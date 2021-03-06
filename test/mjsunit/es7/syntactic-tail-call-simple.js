// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-explicit-tailcalls --stack-size=100

//
// Tail call normal functions.
//
(function() {
  function f(n) {
    if (n <= 0) {
      return "foo";
    }
    return continue f(n - 1);
  }
  assertEquals("foo", f(1e5));
  %OptimizeFunctionOnNextCall(f);
  assertEquals("foo", f(1e5));
})();


(function() {
  function f(n) {
    if (n <= 0) {
      return  "foo";
    }
    return continue f(n - 1, 42);  // Call with arguments adaptor.
  }
  assertEquals("foo", f(1e5));
  %OptimizeFunctionOnNextCall(f);
  assertEquals("foo", f(1e5));
})();


(function() {
  function f(n){
    if (n <= 0) {
      return "foo";
    }
    return continue g(n - 1);
  }
  function g(n){
    if (n <= 0) {
      return "bar";
    }
    return continue f(n - 1);
  }
  assertEquals("foo", f(1e5));
  assertEquals("bar", f(1e5 + 1));
  %OptimizeFunctionOnNextCall(f);
  assertEquals("foo", f(1e5));
  assertEquals("bar", f(1e5 + 1));
})();


(function() {
  function f(n){
    if (n <= 0) {
      return "foo";
    }
    return continue g(n - 1, 42);  // Call with arguments adaptor.
  }
  function g(n){
    if (n <= 0) {
      return "bar";
    }
    return continue f(n - 1, 42);  // Call with arguments adaptor.
  }
  assertEquals("foo", f(1e5));
  assertEquals("bar", f(1e5 + 1));
  %OptimizeFunctionOnNextCall(f);
  assertEquals("foo", f(1e5));
  assertEquals("bar", f(1e5 + 1));
})();


//
// Tail call bound functions.
//
(function() {
  function f0(n) {
    if (n <= 0) {
      return "foo";
    }
    return continue f_bound(n - 1);
  }
  var f_bound = f0.bind({});
  function f(n) {
    return continue f_bound(n);
  }
  assertEquals("foo", f(1e5));
  %OptimizeFunctionOnNextCall(f);
  assertEquals("foo", f(1e5));
})();


(function() {
  function f0(n){
    if (n <= 0) {
      return "foo";
    }
    return continue g_bound(n - 1);
  }
  function g0(n){
    if (n <= 0) {
      return "bar";
    }
    return continue f_bound(n - 1);
  }
  var f_bound = f0.bind({});
  var g_bound = g0.bind({});
  function f(n) {
    return continue f_bound(n);
  }
  assertEquals("foo", f(1e5));
  assertEquals("bar", f(1e5 + 1));
  %OptimizeFunctionOnNextCall(f);
  assertEquals("foo", f(1e5));
  assertEquals("bar", f(1e5 + 1));
})();
