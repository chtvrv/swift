// RUN: %target-swift-frontend -disable-availability-checking -enable-experimental-async-top-level -warn-concurrency -typecheck -verify %s

var a = 10 // expected-note{{var declared here}}

@MainActor
var b = 15 // expected-error{{top-level code variables cannot have a global actor}}

func unsafeAccess() { // expected-note{{add '@MainActor' to make global function 'unsafeAccess()' part of global actor 'MainActor'}}
    print(a) // expected-error@:11{{var 'a' isolated to global actor 'MainActor' can not be referenced from this synchronous context}}
}

func unsafeAsyncAccess() async {
    print(a) // expected-error@:5{{expression is 'async' but is not marked with 'await'}}{{5-5=await }}
             // expected-note@-1:11{{property access is 'async'}}
}

@MainActor
func safeAccess() {
    print(a)
}

@MainActor
func safeSyncAccess() async {
    print(a)
}

func safeAsyncAccess() async {
    await print(a)
}

print(a)
