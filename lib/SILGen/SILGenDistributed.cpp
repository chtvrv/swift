//===--- SILGenDistributed.cpp - SILGen for distributed -------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 - 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "ArgumentSource.h"
#include "Conversion.h"
#include "ExecutorBreadcrumb.h"
#include "Initialization.h"
#include "LValue.h"
#include "RValue.h"
#include "SILGenFunction.h"
#include "SILGenFunctionBuilder.h"
#include "Scope.h"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/DistributedDecl.h"
#include "swift/AST/ForeignErrorConvention.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/PropertyWrappers.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/ProtocolConformanceRef.h"
#include "swift/Basic/Defer.h"
#include "swift/SIL/SILArgument.h"
#include "swift/SIL/SILDeclRef.h"
#include "swift/SIL/TypeLowering.h"
#include "swift/SILOptimizer/Utils/DistributedActor.h"

using namespace swift;
using namespace Lowering;

// MARK: utility functions

/// Obtain a nominal type's member by name, as a VarDecl.
/// \returns nullptr if the name lookup doesn't resolve to exactly one member,
///          or the subsequent cast to VarDecl failed.
static VarDecl* lookupProperty(NominalTypeDecl *decl, DeclName name) {
  assert(decl && "decl was null");
  if (auto clazz = dyn_cast<ClassDecl>(decl)) {
    auto refs = decl->lookupDirect(name);
    if (refs.size() != 1)
      return nullptr;
    return dyn_cast<VarDecl>(refs.front());
  }
  
  return nullptr;
}

/// Emit a reference to a specific stored property of the actor.
static SILValue emitActorPropertyReference(
    SILGenFunction &SGF, SILLocation loc, SILValue actorSelf,
    VarDecl *property) {
  assert(property);
  Type formalType = SGF.F.mapTypeIntoContext(property->getInterfaceType());
  SILType loweredType = SGF.getLoweredType(formalType).getAddressType();
  return SGF.B.createRefElementAddr(loc, actorSelf, property, loweredType);
}

/// Perform an initializing store to the given property using the value
/// \param actorSelf the value representing `self` for the actor instance.
/// \param prop the property to be initialized.
/// \param value the value to use when initializing the property.
static void initializeProperty(SILGenFunction &SGF, SILLocation loc,
                               SILValue actorSelf,
                               VarDecl* prop, SILValue value) {
  Type formalType = SGF.F.mapTypeIntoContext(prop->getInterfaceType());
  SILType loweredType = SGF.getLoweredType(formalType);

  auto fieldAddr = emitActorPropertyReference(SGF, loc, actorSelf, prop);

  if (loweredType.isAddressOnly(SGF.F)) {
    SGF.B.createCopyAddr(loc, value, fieldAddr, IsNotTake, IsInitialization);
  } else {
    if (value->getType().isAddress()) {
      value = SGF.B.createTrivialLoadOr(
          loc, value, LoadOwnershipQualifier::Take);
    } else {
      value = SGF.B.emitCopyValueOperation(loc, value);
    }

    SGF.B.emitStoreValueOperation(
        loc, value, fieldAddr, StoreOwnershipQualifier::Init);

    if (value->getType().isAddress()) {
      SGF.B.createDestroyAddr(loc, value);
    }
  }
}

/******************************************************************************/
/******************* COMMON (DISTRIBUTED) SIL PATTERNS ************************/
/******************************************************************************/

/// Emit the following branch SIL instruction:
/// \verbatim
/// if __isRemoteActor(self) {
///   <isRemoteBB>
/// } else {
///   <isLocalBB>
/// }
/// \endverbatim
static void emitDistributedIfRemoteBranch(SILGenFunction &SGF, SILLocation Loc,
                                          ManagedValue selfValue, Type selfTy,
                                          SILBasicBlock *isRemoteBB,
                                          SILBasicBlock *isLocalBB) {
  ASTContext &ctx = SGF.getASTContext();
  auto &B = SGF.B;

  FuncDecl *isRemoteFn = ctx.getIsRemoteDistributedActor();
  assert(isRemoteFn && "Could not find 'is remote' function, is the "
                       "'Distributed' module available?");

  ManagedValue selfAnyObject = B.createInitExistentialRef(
      Loc, SGF.getLoweredType(ctx.getAnyObjectType()), CanType(selfTy),
      selfValue, {});
  auto result = SGF.emitApplyOfLibraryIntrinsic(
      Loc, isRemoteFn, SubstitutionMap(), {selfAnyObject}, SGFContext());

  SILValue isRemoteResult = std::move(result).forwardAsSingleValue(SGF, Loc);
  SILValue isRemoteResultUnwrapped =
      SGF.emitUnwrapIntegerResult(Loc, isRemoteResult);

  B.createCondBranch(Loc, isRemoteResultUnwrapped, isRemoteBB, isLocalBB);
}

// ==== ------------------------------------------------------------------------
// MARK: local instance initialization

/// For the initialization of a local distributed actor instance, emits code to
/// initialize the instance's stored property corresponding to the system.
static void emitActorSystemInit(SILGenFunction &SGF,
                                        ConstructorDecl *ctor,
                                        SILLocation loc,
                                        ManagedValue actorSelf) {
  auto *dc = ctor->getDeclContext();
  auto classDecl = dc->getSelfClassDecl();
  auto &C = ctor->getASTContext();

  // Sema has already guaranteed that there is exactly one DistributedActorSystem
  // argument to the constructor, so we grab the first one from the params.
  SILValue systemArg = findFirstDistributedActorSystemArg(SGF.F);
  VarDecl *var = lookupProperty(classDecl, C.Id_actorSystem);
  assert(var);
      
  initializeProperty(SGF, loc, actorSelf.getValue(), var, systemArg);
}

/// Emits the distributed actor's identity (`id`) initialization.
///
/// Specifically, it performs:
/// \verbatim
///     self.id = system.assignID(Self.self)
/// \endverbatim
static void emitIDInit(SILGenFunction &SGF, ConstructorDecl *ctor,
                             SILLocation loc, ManagedValue borrowedSelfArg) {
  auto &C = ctor->getASTContext();
  auto &B = SGF.B;
  auto &F = SGF.F;
  
  auto *dc = ctor->getDeclContext();
  auto classDecl = dc->getSelfClassDecl();

  // --- prepare `Self.self` metatype
  auto *selfTyDecl = ctor->getParent()->getSelfNominalTypeDecl();
  auto selfTy = F.mapTypeIntoContext(selfTyDecl->getDeclaredInterfaceType());
  auto selfMetatype = SGF.getLoweredType(MetatypeType::get(selfTy));
  SILValue selfMetatypeValue = B.createMetatype(loc, selfMetatype);

  SILValue actorSystem = findFirstDistributedActorSystemArg(F);

  // --- create a temporary storage for the result of the call
  // it will be deallocated automatically as we exit this scope
  VarDecl *var = lookupProperty(classDecl, C.Id_id);
  auto resultTy = SGF.getLoweredType(
      F.mapTypeIntoContext(var->getInterfaceType()));
  auto temp = SGF.emitTemporaryAllocation(loc, resultTy);

  // --- emit the call itself.
  emitDistributedActorSystemWitnessCall(
      B, loc, C.Id_assignID,
      actorSystem, SGF.getLoweredType(selfTy),
      { temp, selfMetatypeValue });

  // --- initialize the property.
  initializeProperty(SGF, loc, borrowedSelfArg.getValue(), var, temp);
}

namespace {
/// Cleanup to resign the identity of a distributed actor if an abnormal exit happens.
class ResignIdentity : public Cleanup {
  ClassDecl *actorDecl;
  SILValue self;
public:
  ResignIdentity(ClassDecl *actorDecl, SILValue self)
    : actorDecl(actorDecl), self(self) {
      assert(actorDecl->isDistributedActor());
    }

  void emit(SILGenFunction &SGF, CleanupLocation l, ForUnwind_t forUnwind) override {
    if (forUnwind == IsForUnwind) {
      l.markAutoGenerated();
      SGF.emitDistributedActorSystemResignIDCall(l, actorDecl,
                                 ManagedValue::forUnmanaged(self));
    }
  }

  void dump(SILGenFunction &SGF) const override {
#ifndef NDEBUG
    llvm::errs() << "ResignIdentity "
                 << "State:" << getState() << " "
                 << "Self: " << self << "\n";
#endif
  }
};
} // end anonymous namespace

void SILGenFunction::emitDistributedActorImplicitPropertyInits(
    ConstructorDecl *ctor, ManagedValue selfArg) {
  // Only designated initializers should perform this initialization.
  assert(ctor->isDesignatedInit());

  auto loc = SILLocation(ctor);
  loc.markAutoGenerated();

  selfArg = selfArg.borrow(*this, loc);
  emitActorSystemInit(*this, ctor, loc, selfArg);
  emitIDInit(*this, ctor, loc, selfArg);

  // register a clean-up to resign the identity upon abnormal exit
  auto *actorDecl = cast<ClassDecl>(ctor->getParent()->getAsDecl());
  Cleanups.pushCleanup<ResignIdentity>(actorDecl, selfArg.getValue());
}

void SILGenFunction::emitDistributedActorReady(
    SILLocation loc, ConstructorDecl *ctor, ManagedValue actorSelf) {

  // Only designated initializers get the lifecycle handling injected
  assert(ctor->isDesignatedInit());

  SILValue transport = findFirstDistributedActorSystemArg(F);

  FullExpr scope(Cleanups, CleanupLocation(loc));
  auto borrowedSelf = actorSelf.borrow(*this, loc);

  emitActorReadyCall(B, loc, borrowedSelf.getValue(), transport);
}

// MARK: remote instance initialization

/// Synthesize the distributed actor's identity (`id`) initialization:
///
/// \verbatim
///     system.resolve(id:as:)
/// \endverbatim
static void createDistributedActorFactory_resolve(
    SILGenFunction &SGF, ASTContext &C, FuncDecl *fd, SILValue idValue,
    SILValue actorSystemValue, Type selfTy, SILValue selfMetatypeValue,
    SILType resultTy, SILBasicBlock *normalBB, SILBasicBlock *errorBB) {
  auto &B = SGF.B;

  auto loc = SILLocation(fd);
  loc.markAutoGenerated();

  // // ---- actually call system.resolve(id: id, as: Self.self)
  emitDistributedActorSystemWitnessCall(
      B, loc, C.Id_resolve, actorSystemValue, SGF.getLoweredType(selfTy),
      { idValue, selfMetatypeValue },
      std::make_pair(normalBB, errorBB));
}

/// Function body of:
/// \verbatim
/// DistributedActor.resolve(
///     id: Self.ID,
///     using system: Self.ActorSystem
/// ) throws -> Self
/// \endverbatim
void SILGenFunction::emitDistributedActorFactory(FuncDecl *fd) { // TODO(distributed): rename
  /// NOTE: this will only be reached if the resolve function is actually
  ///       demanded. For example, by declaring the actor as `public` or
  ///       having at least one call to the resolve function.

  auto &C = getASTContext();
  SILLocation loc = fd;

  // ==== Prepare argument references
  // --- Parameter: id
  SILArgument *idArg = F.getArgument(0);

  // --- Parameter: system
  SILArgument *actorSystemArg = F.getArgument(1);

  SILValue selfArgValue = F.getSelfArgument();
  ManagedValue selfArg = ManagedValue::forUnmanaged(selfArgValue);

  // type: SpecificDistributedActor.Type
  auto selfArgType = F.mapTypeIntoContext(selfArg.getType().getASTType());
  auto selfMetatype = getLoweredType(selfArgType);
  SILValue selfMetatypeValue = B.createMetatype(loc, selfMetatype);

  // type: SpecificDistributedActor
  auto *selfTyDecl = fd->getParent()->getSelfNominalTypeDecl();
  assert(selfTyDecl->isDistributedActor());
  auto selfTy = F.mapTypeIntoContext(selfTyDecl->getDeclaredInterfaceType());
  auto returnTy = getLoweredType(selfTy);

  // ==== Prepare all the basic blocks
  auto returnBB = createBasicBlock("returnBB");
  auto resolvedBB = createBasicBlock("resolvedBB");
  auto makeProxyBB = createBasicBlock("makeProxyBB");
  auto switchBB = createBasicBlock("switchBB");
  auto errorBB = createBasicBlock("errorBB");

  SILFunctionConventions fnConv = F.getConventions();

  // --- get the uninitialized allocation from the runtime system.
  FullExpr scope(Cleanups, CleanupLocation(fd));

  auto optionalReturnTy = SILType::getOptionalType(returnTy);

  // ==== Call `try system.resolve(id: id, as: Self.self)`
  {
    createDistributedActorFactory_resolve(
        *this, C, fd, idArg, actorSystemArg, selfTy, selfMetatypeValue,
        optionalReturnTy, switchBB, errorBB);
  }

  // ==== switch resolved { ... }
  {
    B.emitBlock(switchBB);
    auto resolve =
        switchBB->createPhiArgument(optionalReturnTy, OwnershipKind::Owned);

    auto *switchEnum = B.createSwitchEnum(
        loc, resolve, nullptr,
        {{C.getOptionalSomeDecl(), resolvedBB},
         {std::make_pair(C.getOptionalNoneDecl(), makeProxyBB)}});
    switchEnum->createOptionalSomeResult();
  }

  // ==== Case 'some') return the resolved instance
  {
    B.emitBlock(resolvedBB);

    B.createBranch(loc, returnBB, {resolvedBB->getArgument(0)});
  }

  // ==== Case 'none') Create the remote instance
  {
    B.emitBlock(makeProxyBB);
    // ==== Create 'remote' distributed actor instance

    // --- Call: _distributedActorRemoteInitialize(Self.self)
    auto builtinName = C.getIdentifier(
        getBuiltinName(BuiltinValueKind::InitializeDistributedRemoteActor));
    auto *remote = B.createBuiltin(
        loc, builtinName,
        /*returnTy*/returnTy,
        /*subs*/ {},
        {selfMetatypeValue});

    // ==== Initialize distributed actor properties
    loc.markAutoGenerated();
    auto *dc = fd->getDeclContext();
    auto classDecl = dc->getSelfClassDecl();
    
    initializeProperty(*this, loc, remote,
                       lookupProperty(classDecl, C.Id_id),
                       idArg);

    initializeProperty(*this, loc, remote,
                       lookupProperty(classDecl, C.Id_actorSystem),
                       actorSystemArg);

    // ==== Branch to return the fully initialized remote instance
    B.createBranch(loc, returnBB, {remote});
  }

  // --- Emit return logic
  // return <remote>
  {
    B.emitBlock(returnBB);

    auto local = returnBB->createPhiArgument(returnTy, OwnershipKind::Owned);

    Cleanups.emitCleanupsForReturn(CleanupLocation(loc), NotForUnwind);
    B.createReturn(loc, local);
  }

  // --- Emit rethrow logic
  // throw error
  {
    B.emitBlock(errorBB);

    auto error = errorBB->createPhiArgument(
        fnConv.getSILErrorType(F.getTypeExpansionContext()),
        OwnershipKind::Owned);

    Cleanups.emitCleanupsForReturn(CleanupLocation(loc), IsForUnwind);
    B.createThrow(loc, error);
  }
}

// MARK: system.resignID()

void SILGenFunction::emitDistributedActorSystemResignIDCall(
    SILLocation loc, ClassDecl *actorDecl, ManagedValue actorSelf) {
  ASTContext &ctx = getASTContext();
  
  FormalEvaluationScope scope(*this);

  // ==== locate: self.id
  auto idRef = emitActorPropertyReference(
      *this, loc, actorSelf.getValue(), lookupProperty(actorDecl, ctx.Id_id));

  // ==== locate: self.actorSystem
  auto systemRef = emitActorPropertyReference(
      *this, loc, actorSelf.getValue(),
      lookupProperty(actorDecl, ctx.Id_actorSystem));

  // Perform the call.
  emitDistributedActorSystemWitnessCall(
      B, loc, ctx.Id_resignID,
      systemRef,
      SILType(),
      { idRef });
}

void
SILGenFunction::emitConditionalResignIdentityCall(SILLocation loc,
                                                  ClassDecl *actorDecl,
                                                  ManagedValue actorSelf,
                                                  SILBasicBlock *continueBB) {
  assert(actorDecl->isDistributedActor() &&
  "only distributed actors have transport lifecycle hooks in deinit");

  auto selfTy = actorDecl->getDeclaredInterfaceType();
  
  // we only system.resignID if we are a local actor,
  // and thus the address was created by system.assignID.
  auto isRemoteBB = createBasicBlock("isRemoteBB");
  auto isLocalBB = createBasicBlock("isLocalBB");

  // if __isRemoteActor(self) {
  //   ...
  // } else {
  //   ...
  // }
  emitDistributedIfRemoteBranch(*this, loc,
                                actorSelf, selfTy,
                                /*if remote*/isRemoteBB,
                                /*if local*/isLocalBB);

  // if remote, do nothing.
  {
    B.emitBlock(isRemoteBB);
    B.createBranch(loc, continueBB);
  }

  // if local, resign identity.
  {
    B.emitBlock(isLocalBB);

    emitDistributedActorSystemResignIDCall(loc, actorDecl, actorSelf);
    
    B.createBranch(loc, continueBB);
  }
}

/******************************************************************************/
/******************* DISTRIBUTED DEINIT: class memberwise destruction *********/
/******************************************************************************/

void SILGenFunction::emitDistributedActorClassMemberDestruction(
    SILLocation cleanupLoc, ManagedValue selfValue, ClassDecl *cd,
    SILBasicBlock *normalMemberDestroyBB, SILBasicBlock *finishBB) {
  auto selfTy = cd->getDeclaredInterfaceType();

  Scope scope(Cleanups, CleanupLocation(cleanupLoc));

  auto isLocalBB = createBasicBlock("isLocalBB");
  auto remoteMemberDestroyBB = createBasicBlock("remoteMemberDestroyBB");

  // if __isRemoteActor(self) {
  //   ...
  // } else {
  //   ...
  // }
  emitDistributedIfRemoteBranch(*this, cleanupLoc,
                                selfValue, selfTy,
                                /*if remote*/remoteMemberDestroyBB,
                                /*if local*/isLocalBB);

  // // if __isRemoteActor(self)
  // {
  //  // destroy only self.id and self.actorSystem
  // }
  {
    B.emitBlock(remoteMemberDestroyBB);

    for (VarDecl *vd : cd->getStoredProperties()) {
      if (getActorIsolation(vd) == ActorIsolation::ActorInstance)
        continue;

      destroyClassMember(cleanupLoc, selfValue, vd);
    }

    B.createBranch(cleanupLoc, finishBB);
  }

  // // else (local distributed actor)
  // {
  //   <continue normal deinit>
  // }
  {
    B.emitBlock(isLocalBB);

    B.createBranch(cleanupLoc, normalMemberDestroyBB);
  }
}
