import XSSandbox from "..";
import { strict as assert } from 'assert';

test('basic', async () => {
  const sandbox = await XSSandbox.create();
  let message: any;
  sandbox.receiveMessage = (m) => message = m;
  sandbox.evaluate("sendMessage({type: 'hello', message: 'world'});");
  assert.deepEqual(message, { type: 'hello', message: 'world' });
});