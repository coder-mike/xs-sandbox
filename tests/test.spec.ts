import XSSandbox from "..";
import { strict as assert } from 'assert';

test('eval', async () => {
  const sandbox = await XSSandbox.create();
  const result = sandbox.evaluate("1 + 1");
  assert.deepEqual(result, 2);
});

test('message to host', async () => {
  const sandbox = await XSSandbox.create();
  let message: any;
  sandbox.receiveMessage = (m) => message = m;
  sandbox.evaluate("sendMessage({type: 'hello', message: 'world'});");
  assert.deepEqual(message, { type: 'hello', message: 'world' });
});

test('message to guest', async () => {
  const sandbox = await XSSandbox.create();
  sandbox.evaluate(`receiveMessage = function(message) {
    return 'Hey, ' + message.message;
  }`);
  const response = sandbox.sendMessage({ type: 'hello', message: 'world' });
  assert.deepEqual(response, 'Hey, world');
});

test('message both ways', async () => {
  const sandbox = await XSSandbox.create();
  let message: any;
  sandbox.receiveMessage = (m) => message = m;
  sandbox.evaluate(`receiveMessage = function(message) {
    sendMessage({ received: message });
  }`);
  sandbox.sendMessage({ type: 'hello', message: 'world' });
  assert.deepEqual(message, { received: { type: 'hello', message: 'world' } });
});

test('guest exception', async () => {
  const sandbox = await XSSandbox.create();
  assert.throws(
    () => sandbox.evaluate(`throw new Error('Guest error')`),
    { message: 'Guest error' }
  );
});

test('guest non-error exception', async () => {
  const sandbox = await XSSandbox.create();
  assert.throws(
    () => sandbox.evaluate(`throw 42`),
    { message: '42' }
  );
});

test('host exception', async () => {
  const sandbox = await XSSandbox.create();

  sandbox.receiveMessage = (m) => {
    throw new Error('dummy error');
  };
  assert.throws(
    () => sandbox.evaluate("sendMessage({type: 'hello', message: 'world'}); 42"),
    { message: 'dummy error' }
  )
});

test('host non-error exception', async () => {
  const sandbox = await XSSandbox.create();

  sandbox.receiveMessage = (m) => {
    throw 123;
  };
  assert.throws(
    () => sandbox.evaluate("sendMessage({type: 'hello', message: 'world'}); 42"),
    { message: '123' }
  )
});

test('take snapshot', async () => {
  const sandbox = await XSSandbox.create();
  let result = sandbox.evaluate("var i = 1");
  result = sandbox.evaluate("++i");
  assert.deepEqual(result, 2);

  const snapshot = sandbox.snapshot();
  assert(snapshot instanceof Uint8Array);
  assert(snapshot.length > 0);
  // Roughly the size I expect it to be. May need to change this if we upgrade
  // the XS source.
  assert(snapshot.length > 87000 && snapshot.length < 88000);
});

test('take and restore snapshot', async () => {
  const sandbox1 = await XSSandbox.create();
  let result = sandbox1.evaluate("var i = 1");
  result = sandbox1.evaluate("++i");
  assert.deepEqual(result, 2);

  const snapshot = sandbox1.snapshot();
  const sandbox2 = await XSSandbox.restore(snapshot);

  result = sandbox2.evaluate("++i");
  assert.deepEqual(result, 3);
});

test('event loop', async () => {
  // This tests that the event loop is flushed before `sendMessage` returns
  const sandbox = await XSSandbox.create();
  const received: any[] = [];
  sandbox.receiveMessage = (m) => received.push(m);
  sandbox.evaluate(`receiveMessage = function(message) {
    (async () => {
      sendMessage('before await');
      await Promise.resolve();
      sendMessage('after await');
    })();
    sendMessage('after async func call');
  }`);
  sandbox.sendMessage(null);
  assert.deepEqual(received, [
    'before await',
    'after async func call',
    'after await'
  ]);
});

test('promises across snapshots', async () => {
  // This test simulates the situation where a host operation is awaited by the
  // guest, and the guest is put to sleep before the host operation completes,
  // and then woken up to process the result of the host operation in a new
  // sandbox.

  const sandbox1 = await XSSandbox.create();

  const receivedMessages1: any[] = [];

  sandbox1.receiveMessage = function (message) {
    receivedMessages1.push(message);
  }

  sandbox1.evaluate(`
    let nextTaskId = 1;
    const pendingAsyncTasks = new Map();
    function log1(s) {
      sendMessage({ type: 'log', message: s });
    }
    function hostAsyncTask() {
      return new Promise((resolve, reject) => {
        const taskId = nextTaskId++;
        sendMessage({ type: 'hostAsyncTask', taskId });
        pendingAsyncTasks.set(taskId, { resolve, reject });
      });
    }
    async function longTask() {
      log1('before await');
      await hostAsyncTask();
      log1('after await');
    }
    receiveMessage = function(message) {
      if (message.type === 'hostAsyncTask') {
        longTask();
      } else if (message.type === 'resolveAsyncTask') {
        const task = pendingAsyncTasks.get(message.taskId);
        task.resolve(message.result);
      }
    }
  `);

  sandbox1.sendMessage({ type: 'hostAsyncTask' });
  assert.deepEqual(receivedMessages1, [
    { type: 'log', message: 'before await' },
    { type: 'hostAsyncTask', taskId: 1 },
  ]);

  const snapshot = sandbox1.snapshot();
  const sandbox2 = await XSSandbox.restore(snapshot);
  const receivedMessages2: any[] = [];
  sandbox2.receiveMessage = function (message) {
    receivedMessages2.push(message);
  };

  // Send result of async task
  sandbox2.sendMessage({
    type: 'resolveAsyncTask',
    taskId: 1,
    result: 'result'
  });
  assert.deepEqual(receivedMessages2, [
    { type: 'log', message: 'after await' }
  ]);
});

test('meter measurement', async () => {
  // The meter values in this test are empirically determined. They may change
  // if we upgrade the XS source.

  const sandbox = await XSSandbox.create();
  const meterValues: number[] = [];
  sandbox.receiveMessage = m => meterValues.push(sandbox.meter);
  assert.equal(sandbox.meter, 0);
  sandbox.evaluate(`for (let i = 0; i < 10; i++) {
    sendMessage(null);
  }`);
  // To make it easy to determine what kind of meter limit to set, the meter is
  // sticky. It doesn't reset to zero when control returns to the host.
  assert.equal(sandbox.meter, 322);

  assert.deepEqual(meterValues, [35,64,93,122,151,180,209,238,267,296]);
});

test('meter limit', async () => {
  const sandbox = await XSSandbox.create({
    meteringInterval: 1,
    meteringLimit: 150
  });
  assert.equal(sandbox.meteringInterval, 1);
  assert.equal(sandbox.meteringLimit, 150);
  const meterValues: number[] = [];
  sandbox.receiveMessage = m => meterValues.push(sandbox.meter);
  assert.equal(sandbox.meter, 0);
  assert.throws(() => {
    sandbox.evaluate(`for (;;) {
      sendMessage(null);
    }`),
    'Metering limit reached'
  });
  // To make it easy to determine what kind of meter limit to set, the meter is
  // sticky. It doesn't reset to zero when control returns to the host.
  assert.equal(sandbox.meter, 162);

  assert.deepEqual(meterValues, [25,44,63,82,101,120,139,158]);
});

test('meter ignores guest catch blocks', async () => {
  const sandbox = await XSSandbox.create({
    meteringInterval: 1,
    meteringLimit: 150
  });
  assert.equal(sandbox.meteringInterval, 1);
  assert.equal(sandbox.meteringLimit, 150);
  const messages: string[] = [];
  sandbox.receiveMessage = m => messages.push(m);
  assert.equal(sandbox.meter, 0);
  assert.throws(() => {
    sandbox.evaluate(`
      try {
        for (;;) ;
      catch (e) {
        sendMessage('caught');
      }
    }`),
    'Metering limit reached'
  });
  // The catch statement never executed
  assert.deepEqual(messages, []);
});

test('return value from host receiveMessage', async () => {
  const sandbox = await XSSandbox.create();
  sandbox.receiveMessage = m => `host received: ${m}`;
  sandbox.evaluate(`receiveMessage = function(message) {
    return 'guest received response: ' + sendMessage('guest received: ' + message);
  }`);
  const response = sandbox.sendMessage('hello');
  assert.deepEqual(response, 'guest received response: host received: guest received: hello');
});

test('return undefined from host receiveMessage', async () => {
  const sandbox = await XSSandbox.create();
  sandbox.receiveMessage = m => undefined;
  sandbox.evaluate(`receiveMessage = function(message) {
    const result = sendMessage(message)

    return 'result was ' + (result === undefined ? 'undefined' : 'not undefined');
  }`);
  const response = sandbox.sendMessage('hello');
  assert.deepEqual(response, 'result was undefined');
});

test('exception stack', async () => {
  const sandbox = await XSSandbox.create();
  sandbox.evaluate(`
    receiveMessage = function(message) {
      throw new Error('guest error');
    }
  `)
  assert.throws(
    () => sandbox.sendMessage('hello'),
    { stack: 'Error: guest error\n at receiveMessage (#0:3)' }
  );
});

test('console.log', async() => {
  const sandbox = await XSSandbox.create();
  let consoleOutput: string[][] = [];
  const temp = console.log;
  console.log = (...args) => consoleOutput.push(args);
  try {
    sandbox.evaluate(`
      console.log('hello', 42);
      console.log('world');
    `);
    assert.deepEqual(consoleOutput, [['hello', 42], ['world']]);
  } finally {
    console.log = temp;
  }
})

test('meter expired in run loop', async () => {
  const sandbox = await XSSandbox.create({
    meteringInterval: 1,
    meteringLimit: 1000
  });
  sandbox.evaluate(`
    receiveMessage = function() {
      run();
      return 42;
    }
    async function run() {
      await Promise.resolve();
      while(true) ;
    }
  `);

  assert.throws(
    () => sandbox.sendMessage(null),
    { message: 'Metering limit reached' }
  );
});