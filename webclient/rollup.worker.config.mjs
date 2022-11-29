import typescript from '@rollup/plugin-typescript';

export default {
  input: 'loader/MeshLoaderWorker.ts',
  output: {
    dir: 'dist',  // Rollup complains that this must be in the typescript outDir dir, so we can't set it to dist/loader.
    format: 'iife'
  },
  plugins: [typescript()]
}
