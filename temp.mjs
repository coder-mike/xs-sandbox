import Sandbox from './dist/index.mjs';

const sandbox = await Sandbox.create({
  meteringLimit: 5000,
  meteringInterval: 1000,
});

try {
  sandbox.evaluate('while (true) {}') // Infinite loop
} catch (e) {
  console.log(e); // "metering limit reached"
  console.log(sandbox.meter);
}