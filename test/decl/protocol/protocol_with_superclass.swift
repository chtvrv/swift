// RUN: %target-typecheck-verify-swift

// Protocols with superclass-constrained Self.

class Concrete {
  typealias ConcreteAlias = String

  func concreteMethod(_: ConcreteAlias) {}
}

class Generic<T> : Concrete { // expected-note 6 {{arguments to generic parameter 'T' ('Int' and 'String') are expected to be equal}}
  typealias GenericAlias = (T, T)

  func genericMethod(_: GenericAlias) {}
}

protocol BaseProto {}

protocol ProtoRefinesClass : Generic<Int>, BaseProto {
  func requirementUsesClassTypes(_: ConcreteAlias, _: GenericAlias)
  // expected-note@-1 {{protocol requires function 'requirementUsesClassTypes' with type '(Generic<Int>.ConcreteAlias, Generic<Int>.GenericAlias) -> ()' (aka '(String, (Int, Int)) -> ()'); do you want to add a stub?}}
}

func duplicateOverload<T : ProtoRefinesClass>(_: T) {}
// expected-note@-1 {{'duplicateOverload' previously declared here}}

func duplicateOverload<T : ProtoRefinesClass & Generic<Int>>(_: T) {}
// expected-error@-1 {{invalid redeclaration of 'duplicateOverload'}}
// expected-warning@-2 {{redundant superclass constraint 'T' : 'Generic<Int>'}}
// expected-note@-3 {{superclass constraint 'T' : 'Generic<Int>' implied here}}

extension ProtoRefinesClass {
  func extensionMethodUsesClassTypes(_ x: ConcreteAlias, _ y: GenericAlias) {
    _ = ConcreteAlias.self
    _ = GenericAlias.self

    concreteMethod(x)
    genericMethod(y)

    let _: Generic<Int> = self
    let _: Concrete = self
    let _: BaseProto = self
    let _: BaseProto & Generic<Int> = self
    let _: BaseProto & Concrete = self

    let _: Generic<String> = self
    // expected-error@-1 {{cannot assign value of type 'Generic<Int>' to type 'Generic<String>'}}
  }
}

func usesProtoRefinesClass1(_ t: ProtoRefinesClass) {
  let x: ProtoRefinesClass.ConcreteAlias = "hi"
  _ = ProtoRefinesClass.ConcreteAlias.self

  t.concreteMethod(x)

  let y: ProtoRefinesClass.GenericAlias = (1, 2)
  _ = ProtoRefinesClass.GenericAlias.self

  t.genericMethod(y)

  t.requirementUsesClassTypes(x, y)

  let _: Generic<Int> = t
  let _: Concrete = t
  let _: BaseProto = t
  let _: BaseProto & Generic<Int> = t
  let _: BaseProto & Concrete = t

  let _: Generic<String> = t
  // expected-error@-1 {{cannot assign value of type 'Generic<Int>' to type 'Generic<String>'}}
}

func usesProtoRefinesClass2<T : ProtoRefinesClass>(_ t: T) {
  let x: T.ConcreteAlias = "hi"
  _ = T.ConcreteAlias.self

  t.concreteMethod(x)

  let y: T.GenericAlias = (1, 2)
  _ = T.GenericAlias.self

  t.genericMethod(y)

  t.requirementUsesClassTypes(x, y)

  let _: Generic<Int> = t
  let _: Concrete = t
  let _: BaseProto = t
  let _: BaseProto & Generic<Int> = t
  let _: BaseProto & Concrete = t

  let _: Generic<String> = t
  // expected-error@-1 {{cannot assign value of type 'Generic<Int>' to type 'Generic<String>'}}
}

class BadConformingClass1 : ProtoRefinesClass {
  // expected-error@-1 {{'ProtoRefinesClass' requires that 'BadConformingClass1' inherit from 'Generic<Int>'}}
  // expected-note@-2 {{requirement specified as 'Self' : 'Generic<Int>' [with Self = BadConformingClass1]}}
  func requirementUsesClassTypes(_: ConcreteAlias, _: GenericAlias) {
    // expected-error@-1 {{cannot find type 'ConcreteAlias' in scope}}
    // expected-error@-2 {{cannot find type 'GenericAlias' in scope}}

    _ = ConcreteAlias.self
    // expected-error@-1 {{cannot find 'ConcreteAlias' in scope}}
    _ = GenericAlias.self
    // expected-error@-1 {{cannot find 'GenericAlias' in scope}}
  }
}

class BadConformingClass2 : Generic<String>, ProtoRefinesClass {
  // expected-error@-1 {{'ProtoRefinesClass' requires that 'BadConformingClass2' inherit from 'Generic<Int>'}}
  // expected-note@-2 {{requirement specified as 'Self' : 'Generic<Int>' [with Self = BadConformingClass2]}}
  // expected-error@-3 {{type 'BadConformingClass2' does not conform to protocol 'ProtoRefinesClass'}}
  func requirementUsesClassTypes(_: ConcreteAlias, _: GenericAlias) {
    // expected-note@-1 {{candidate has non-matching type '(BadConformingClass2.ConcreteAlias, BadConformingClass2.GenericAlias) -> ()' (aka '(String, (String, String)) -> ()')}}
    _ = ConcreteAlias.self
    _ = GenericAlias.self
  }
}

class GoodConformingClass : Generic<Int>, ProtoRefinesClass {
  func requirementUsesClassTypes(_ x: ConcreteAlias, _ y: GenericAlias) {
    _ = ConcreteAlias.self
    _ = GenericAlias.self

    concreteMethod(x)

    genericMethod(y)
  }
}

protocol ProtoRefinesProtoWithClass : ProtoRefinesClass {}

extension ProtoRefinesProtoWithClass {
  func anotherExtensionMethodUsesClassTypes(_ x: ConcreteAlias, _ y: GenericAlias) {
    _ = ConcreteAlias.self
    _ = GenericAlias.self

    concreteMethod(x)
    genericMethod(y)

    let _: Generic<Int> = self
    let _: Concrete = self
    let _: BaseProto = self
    let _: BaseProto & Generic<Int> = self
    let _: BaseProto & Concrete = self

    let _: Generic<String> = self
    // expected-error@-1 {{cannot assign value of type 'Generic<Int>' to type 'Generic<String>'}}
  }
}

func usesProtoRefinesProtoWithClass1(_ t: ProtoRefinesProtoWithClass) {
  let x: ProtoRefinesProtoWithClass.ConcreteAlias = "hi"
  _ = ProtoRefinesProtoWithClass.ConcreteAlias.self

  t.concreteMethod(x)

  let y: ProtoRefinesProtoWithClass.GenericAlias = (1, 2)
  _ = ProtoRefinesProtoWithClass.GenericAlias.self

  t.genericMethod(y)

  t.requirementUsesClassTypes(x, y)

  let _: Generic<Int> = t
  let _: Concrete = t
  let _: BaseProto = t
  let _: BaseProto & Generic<Int> = t
  let _: BaseProto & Concrete = t

  let _: Generic<String> = t
  // expected-error@-1 {{cannot assign value of type 'Generic<Int>' to type 'Generic<String>'}}
}

func usesProtoRefinesProtoWithClass2<T : ProtoRefinesProtoWithClass>(_ t: T) {
  let x: T.ConcreteAlias = "hi"
  _ = T.ConcreteAlias.self

  t.concreteMethod(x)

  let y: T.GenericAlias = (1, 2)
  _ = T.GenericAlias.self

  t.genericMethod(y)

  t.requirementUsesClassTypes(x, y)

  let _: Generic<Int> = t
  let _: Concrete = t
  let _: BaseProto = t
  let _: BaseProto & Generic<Int> = t
  let _: BaseProto & Concrete = t

  let _: Generic<String> = t
  // expected-error@-1 {{cannot assign value of type 'Generic<Int>' to type 'Generic<String>'}}
}

class ClassWithInits<T> {
  init(notRequiredInit: ()) {}
  // expected-note@-1 6{{selected non-required initializer 'init(notRequiredInit:)'}}

  required init(requiredInit: ()) {}
}

protocol ProtocolWithClassInits : ClassWithInits<Int> {}

func useProtocolWithClassInits1() {
  _ = ProtocolWithClassInits(notRequiredInit: ())
  // expected-error@-1 {{type 'any ProtocolWithClassInits' cannot be instantiated}}

  _ = ProtocolWithClassInits(requiredInit: ())
  // expected-error@-1 {{type 'any ProtocolWithClassInits' cannot be instantiated}}
}

func useProtocolWithClassInits2(_ t: ProtocolWithClassInits.Type) {
  _ = t.init(notRequiredInit: ())
  // expected-error@-1 {{constructing an object of class type 'any ProtocolWithClassInits' with a metatype value must use a 'required' initializer}}

  let _: ProtocolWithClassInits = t.init(requiredInit: ())
}

func useProtocolWithClassInits3<T : ProtocolWithClassInits>(_ t: T.Type) {
  _ = T(notRequiredInit: ())
  // expected-error@-1 {{constructing an object of class type 'T' with a metatype value must use a 'required' initializer}}

  let _: T = T(requiredInit: ())

  _ = t.init(notRequiredInit: ())
  // expected-error@-1 {{constructing an object of class type 'T' with a metatype value must use a 'required' initializer}}

  let _: T = t.init(requiredInit: ())
}

protocol ProtocolRefinesClassInits : ProtocolWithClassInits {}

func useProtocolRefinesClassInits1() {
  _ = ProtocolRefinesClassInits(notRequiredInit: ())
  // expected-error@-1 {{type 'any ProtocolRefinesClassInits' cannot be instantiated}}

  _ = ProtocolRefinesClassInits(requiredInit: ())
  // expected-error@-1 {{type 'any ProtocolRefinesClassInits' cannot be instantiated}}
}

func useProtocolRefinesClassInits2(_ t: ProtocolRefinesClassInits.Type) {
  _ = t.init(notRequiredInit: ())
  // expected-error@-1 {{constructing an object of class type 'any ProtocolRefinesClassInits' with a metatype value must use a 'required' initializer}}

  let _: ProtocolRefinesClassInits = t.init(requiredInit: ())
}

func useProtocolRefinesClassInits3<T : ProtocolRefinesClassInits>(_ t: T.Type) {
  _ = T(notRequiredInit: ())
  // expected-error@-1 {{constructing an object of class type 'T' with a metatype value must use a 'required' initializer}}

  let _: T = T(requiredInit: ())

  _ = t.init(notRequiredInit: ())
  // expected-error@-1 {{constructing an object of class type 'T' with a metatype value must use a 'required' initializer}}

  let _: T = t.init(requiredInit: ())
}

// Make sure that we don't require 'mutating' when the protocol has a superclass
// constraint.
protocol HasMutableProperty : Concrete {
  var mutableThingy: Any? { get set }
}

extension HasMutableProperty {
  func mutateThingy() {
    mutableThingy = nil
  }
}

// Some pathological examples -- just make sure they don't crash.

protocol RecursiveSelf : Generic<Self> {}
// expected-error@-1 {{superclass constraint 'Self' : 'Generic<Self>' is recursive}}

protocol RecursiveAssociatedType : Generic<Self.X> {
  // expected-error@-1 {{superclass constraint 'Self' : 'Generic<Self.X>' is recursive}}
  associatedtype X
}

protocol BaseProtocol {
  typealias T = Int
}

class BaseClass : BaseProtocol {}

protocol RefinedProtocol : BaseClass {
  func takesT(_: T)
}

func takesBaseProtocol(_: BaseProtocol) {}

func passesRefinedProtocol(_ r: RefinedProtocol) {
  takesBaseProtocol(r)
}

class RefinedClass : BaseClass, RefinedProtocol {
  func takesT(_: T) {
    _ = T.self
  }
}

class LoopClass : LoopProto {}
protocol LoopProto : LoopClass {}

class FirstClass {}
protocol FirstProtocol : FirstClass {}
class SecondClass : FirstClass {}
protocol SecondProtocol : SecondClass, FirstProtocol {}

class FirstConformer : FirstClass, SecondProtocol {}
// expected-error@-1 {{'SecondProtocol' requires that 'FirstConformer' inherit from 'SecondClass'}}
// expected-note@-2 {{requirement specified as 'Self' : 'SecondClass' [with Self = FirstConformer]}}

class SecondConformer : SecondClass, SecondProtocol {}

// Duplicate superclass
// FIXME: Duplicate diagnostics
protocol DuplicateSuper : Concrete, Concrete {}
// expected-note@-1 {{superclass constraint 'Self' : 'Concrete' implied here}}
// expected-warning@-2 {{redundant superclass constraint 'Self' : 'Concrete'}}
// expected-error@-3 {{duplicate inheritance from 'Concrete'}}

// Ambigous name lookup situation
protocol Amb : Concrete {}
// expected-note@-1 {{'Amb' previously declared here}}
// expected-note@-2 {{found this candidate}}
protocol Amb : Concrete {}
// expected-error@-1 {{invalid redeclaration of 'Amb'}}
// expected-note@-2 {{found this candidate}}

extension Amb { // expected-error {{'Amb' is ambiguous for type lookup in this context}}
  func extensionMethodUsesClassTypes() {
    _ = ConcreteAlias.self
  }
}

protocol ProtoRefinesClassComposition : Generic<Int> & BaseProto {}

func usesProtoRefinesClass1(_ t: ProtoRefinesClassComposition) {
  t.genericMethod((1, 2))
  let _: BaseProto = t
}

func usesProtoRefinesClass2<T : ProtoRefinesClassComposition>(_ t: T) {
  t.genericMethod((1, 2))
  let _: BaseProto = t
}
