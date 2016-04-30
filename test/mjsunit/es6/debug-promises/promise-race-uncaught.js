// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-debug-as debug --allow-natives-syntax

// Test debug events when we only listen to uncaught exceptions and a
// Promise p3 created by Promise.race has no catch handler, and is rejected
// because one of the Promises p2 passed to Promise.all is rejected. We
// expect two Exception debug events to be triggered, for p2 and p3 each,
// because neither has an user-defined catch handler.

var Debug = debug.Debug;

var expected_events = 2;
var log = [];

var p1 = Promise.resolve();
p1.name = "p1";

var p2 = p1.then(function() {
  log.push("throw");
  throw new Error("uncaught");  // event
});

p2.name = "p2";

var p3 = Promise.race([p2]);
p3.name = "p3";

function listener(event, exec_state, event_data, data) {
  if (event != Debug.DebugEvent.Exception) return;
  try {
    expected_events--;
    assertTrue(expected_events >= 0);
    assertEquals("uncaught", event_data.exception().message);
    assertTrue(event_data.promise() instanceof Promise);
    if (expected_events === 1) {
      // Assert that the debug event is triggered at the throw site.
      assertTrue(exec_state.frame(0).sourceLineText().indexOf("// event") > 0);
      assertEquals("p2", event_data.promise().name);
    } else {
      assertEquals("p3", event_data.promise().name);
    }
    assertTrue(event_data.uncaught());
  } catch (e) {
    %AbortJS(e + "\n" + e.stack);
  }
}

Debug.setBreakOnUncaughtException();
Debug.setListener(listener);

log.push("end main");

function testDone(iteration) {
  function checkResult() {
    try {
      assertTrue(iteration < 10);
      if (expected_events === 0) {
        assertEquals(["end main", "throw"], log);
      } else {
        testDone(iteration + 1);
      }
    } catch (e) {
      %AbortJS(e + "\n" + e.stack);
    }
  }

  %EnqueueMicrotask(checkResult);
}

testDone(0);
