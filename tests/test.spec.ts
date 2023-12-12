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