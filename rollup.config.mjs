import typescript from '@rollup/plugin-typescript';
import terser from '@rollup/plugin-terser';
import pkg from './package.json' assert { type: "json" };

export default {
  input: 'build/index.mts',
  output: [{
    file: pkg.main,
    name: 'XSSandbox',
    sourcemap: true,
    format: 'umd',
    exports: 'named'
  }, {
    file: pkg.module,
    format: 'es',
    exports: 'named',
    sourcemap: true
  }],
  watch: {
    include: ["build/**/*.mts", "build/**/*.mjs"],
    exclude: "node_modules/**/*",
    chokidar: {
      usePolling: true
    }
  },
  plugins: [
    typescript({
      tsconfig: './tsconfig.json'
    }),
    terser(),
  ]
};