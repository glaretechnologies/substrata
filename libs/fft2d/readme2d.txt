General Purpose 2D FFT (Fast Fourier Transform) Package

files
    fft4f2d.c  : 2D FFT Package in C       - Version I
    fft4f2d.f  : 2D FFT Package in Fortran - Version I
    fft4f2dt.c : Test Program of "fft4f2d.c"
    fft4f2dt.f : Test Program of "fft4f2d.f"
    fft4g.c    : 1D FFT Package in C       - Fast Version II
    fft4g.f    : 1D FFT Package in Fortran - Fast Version II
    fft4g2d.c  : 2D FFT Package in C       - Version II
    fft4g2d.f  : 2D FFT Package in Fortran - Version II
    fft4g2dt.c : Test Program of "fft4g2d.c"
    fft4g2dt.f : Test Program of "fft4g2d.f"
    shrtdct.c  : 8x8, 16x16 DCT Package
    shrtdctt.c : Test Program of "shrtdct.c"

routines in the package
    in fft4f2d.*, fft4g2d.*
        cdft2d: 2-dim Complex Discrete Fourier Transform
        rdft2d: 2-dim Real Discrete Fourier Transform
        ddct2d: 2-dim Discrete Cosine Transform
        ddst2d: 2-dim Discrete Sine Transform
    in fft4g.*
        cdft: 1-dim Complex Discrete Fourier Transform
        rdft: 1-dim Real Discrete Fourier Transform
        ddct: 1-dim Discrete Cosine Transform
        ddst: 1-dim Discrete Sine Transform
        dfct: 1-dim Real Symmetric DFT
        dfst: 1-dim Real Anti-symmetric DFT
        (these routines are called by fft4g2d.*)
    in shrtdct.c
        ddct8x8s  : Normalized 8x8 DCT
        ddct16x16s: Normalized 16x16 DCT
        (faster than ddct2d())

usage
    see block comments of each routines

copyright
    Copyright(C) 1997 Takuya OOURA (email: ooura@mmm.t.u-tokyo.ac.jp).
    You may use, copy, modify this code for any purpose and 
    without fee. You may distribute this ORIGINAL package.

