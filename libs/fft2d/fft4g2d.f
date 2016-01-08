! Fast Fourier/Cosine/Sine Transform
!     dimension   :two
!     data length :power of 2
!     decimation  :frequency
!     radix       :4, 2, row-column
!     data        :inplace
!     table       :use
! subroutines
!     cdft2d: Complex Discrete Fourier Transform
!     rdft2d: Real Discrete Fourier Transform
!     ddct2d: Discrete Cosine Transform
!     ddst2d: Discrete Sine Transform
! necessary package
!     fft4g.f  : 1D-FFT package
!
!
! -------- Complex DFT (Discrete Fourier Transform) --------
!     [definition]
!         <case1>
!             X(k1,k2) = sum_j1=0^n1-1 sum_j2=0^n2-1 x(j1,j2) * 
!                            exp(2*pi*i*j1*k1/n1) * 
!                            exp(2*pi*i*j2*k2/n2), 
!                            0<=k1<n1, 0<=k2<n2
!         <case2>
!             X(k1,k2) = sum_j1=0^n1-1 sum_j2=0^n2-1 x(j1,j2) * 
!                            exp(-2*pi*i*j1*k1/n1) * 
!                            exp(-2*pi*i*j2*k2/n2), 
!                            0<=k1<n1, 0<=k2<n2
!         (notes: sum_j=0^n-1 is a summation from j=0 to n-1)
!     [usage]
!         <case1>
!             ip(0) = 0  ! first time only
!             call cdft2d(n1max, 2*n1, n2, 1, a, t, ip, w)
!         <case2>
!             ip(0) = 0  ! first time only
!             call cdft2d(n1max, 2*n1, n2, -1, a, t, ip, w)
!     [parameters]
!         n1max  :row size of the 2D array (integer)
!         2*n1   :data length (integer)
!                 n1 >= 1, n1 = power of 2
!         n2     :data length (integer)
!                 n2 >= 1, n2 = power of 2
!         a(0:2*n1-1,0:n2-1)
!                :input/output data (real*8)
!                 input data
!                     a(2*j1,j2) = Re(x(j1,j2)), 
!                     a(2*j1+1,j2) = Im(x(j1,j2)), 
!                     0<=j1<n1, 0<=j2<n2
!                 output data
!                     a(2*k1,k2) = Re(X(k1,k2)), 
!                     a(2*k1+1,k2) = Im(X(k1,k2)), 
!                     0<=k1<n1, 0<=k2<n2
!         t(0:2*n2-1)
!                :work area (real*8)
!         ip(0:*):work area for bit reversal (integer)
!                 length of ip >= 2+sqrt(n)  ; if mod(n,4) = 0
!                                 2+sqrt(n/2); otherwise
!                 (n = max(n1, n2))
!                 ip(0),ip(1) are pointers of the cos/sin table.
!         w(0:*) :cos/sin table (real*8)
!                 length of w >= max(n1/2, n2/2)
!                 w(),ip() are initialized if ip(0) = 0.
!     [remark]
!         Inverse of 
!             call cdft2d(n1max, 2*n1, n2, -1, a, t, ip, w)
!         is 
!             call cdft2d(n1max, 2*n1, n2, 1, a, t, ip, w)
!             do j2 = 0, n2 - 1
!                 do j1 = 0, 2 * n1 - 1
!                     a(j1, j2) = a(j1, j2) * (1.0d0 / (n1 * n2))
!                 end do
!             end do
!         .
!
!
! -------- Real DFT / Inverse of Real DFT --------
!     [definition]
!         <case1> RDFT
!             R(k1,k2) = sum_j1=0^n1-1 sum_j2=0^n2-1 a(j1,j2) * 
!                            cos(2*pi*j1*k1/n1 + 2*pi*j2*k2/n2), 
!                            0<=k1<n1, 0<=k2<n2
!             I(k1,k2) = sum_j1=0^n1-1 sum_j2=0^n2-1 a(j1,j2) * 
!                            sin(2*pi*j1*k1/n1 + 2*pi*j2*k2/n2), 
!                            0<=k1<n1, 0<=k2<n2
!         <case2> IRDFT (excluding scale)
!             a(k1,k2) = (1/2) * sum_j1=0^n1-1 sum_j2=0^n2-1
!                            (R(j1,j2) * 
!                            cos(2*pi*j1*k1/n1 + 2*pi*j2*k2/n2) + 
!                            I(j1,j2) * 
!                            sin(2*pi*j1*k1/n1 + 2*pi*j2*k2/n2)), 
!                            0<=k1<n1, 0<=k2<n2
!         (notes: R(n1-k1,n2-k2) = R(k1,k2), 
!                 I(n1-k1,n2-k2) = -I(k1,k2), 
!                 R(n1-k1,0) = R(k1,0), 
!                 I(n1-k1,0) = -I(k1,0), 
!                 R(0,n2-k2) = R(0,k2), 
!                 I(0,n2-k2) = -I(0,k2), 
!                 0<k1<n1, 0<k2<n2)
!     [usage]
!         <case1>
!             ip(0) = 0  ! first time only
!             call rdft2d(n1max, n1, n2, 1, a, t, ip, w)
!         <case2>
!             ip(0) = 0  ! first time only
!             call rdft2d(n1max, n1, n2, -1, a, t, ip, w)
!     [parameters]
!         n1max  :row size of the 2D array (integer)
!         n1     :data length (integer)
!                 n1 >= 2, n1 = power of 2
!         n2     :data length (integer)
!                 n2 >= 2, n2 = power of 2
!         a(0:n1-1,0:n2-1)
!                :input/output data (real*8)
!                 <case1>
!                     output data
!                         a(2*k1,k2) = R(k1,k2) = R(n1-k1,n2-k2), 
!                         a(2*k1+1,k2) = I(k1,k2) = -I(n1-k1,n2-k2), 
!                             0<k1<n1/2, 0<k2<n2, 
!                         a(2*k1,0) = R(k1,0) = R(n1-k1,0), 
!                         a(2*k1+1,0) = I(k1,0) = -I(n1-k1,0), 
!                             0<k1<n1/2, 
!                         a(0,k2) = R(0,k2) = R(0,n2-k2), 
!                         a(1,k2) = I(0,k2) = -I(0,n2-k2), 
!                         a(1,n2-k2) = R(n1/2,k2) = R(n1/2,n2-k2), 
!                         a(0,n2-k2) = -I(n1/2,k2) = I(n1/2,n2-k2), 
!                             0<k2<n2/2, 
!                         a(0,0) = R(0,0), 
!                         a(1,0) = R(n1/2,0), 
!                         a(0,n2/2) = R(0,n2/2), 
!                         a(1,n2/2) = R(n1/2,n2/2)
!                 <case2>
!                     input data
!                         a(2*j1,j2) = R(j1,j2) = R(n1-j1,n2-j2), 
!                         a(2*j1+1,j2) = I(j1,j2) = -I(n1-j1,n2-j2), 
!                             0<j1<n1/2, 0<j2<n2, 
!                         a(2*j1,0) = R(j1,0) = R(n1-j1,0), 
!                         a(2*j1+1,0) = I(j1,0) = -I(n1-j1,0), 
!                             0<j1<n1/2, 
!                         a(0,j2) = R(0,j2) = R(0,n2-j2), 
!                         a(1,j2) = I(0,j2) = -I(0,n2-j2), 
!                         a(1,n2-j2) = R(n1/2,j2) = R(n1/2,n2-j2), 
!                         a(0,n2-j2) = -I(n1/2,j2) = I(n1/2,n2-j2), 
!                             0<j2<n2/2, 
!                         a(0,0) = R(0,0), 
!                         a(1,0) = R(n1/2,0), 
!                         a(0,n2/2) = R(0,n2/2), 
!                         a(1,n2/2) = R(n1/2,n2/2)
!         t(0:2*n2-1)
!                :work area (real*8)
!         ip(0:*):work area for bit reversal (integer)
!                 length of ip >= 2+sqrt(n)  ; if mod(n,4) = 0
!                                 2+sqrt(n/2); otherwise
!                 (n = max(n1/2, n2))
!                 ip(0),ip(1) are pointers of the cos/sin table.
!         w(0:*) :cos/sin table (real*8)
!                 length of w >= max(n1/4, n2/2) + n1/4
!                 w(),ip() are initialized if ip(0) = 0.
!     [remark]
!         Inverse of 
!             call rdft2d(n1max, n1, n2, 1, a, t, ip, w)
!         is 
!             call rdft2d(n1max, n1, n2, -1, a, t, ip, w)
!             do j2 = 0, n2 - 1
!                 do j1 = 0, n1 - 1
!                     a(j1, j2) = a(j1, j2) * (2.0d0 / (n1 * n2))
!                 end do
!             end do
!         .
!
!
! -------- DCT (Discrete Cosine Transform) / Inverse of DCT --------
!     [definition]
!         <case1> IDCT (excluding scale)
!             C(k1,k2) = sum_j1=0^n1-1 sum_j2=0^n2-1 a(j1,j2) * 
!                            cos(pi*j1*(k1+1/2)/n1) * 
!                            cos(pi*j2*(k2+1/2)/n2), 
!                            0<=k1<n1, 0<=k2<n2
!         <case2> DCT
!             C(k1,k2) = sum_j1=0^n1-1 sum_j2=0^n2-1 a(j1,j2) * 
!                            cos(pi*(j1+1/2)*k1/n1) * 
!                            cos(pi*(j2+1/2)*k2/n2), 
!                            0<=k1<n1, 0<=k2<n2
!     [usage]
!         <case1>
!             ip(0) = 0  ! first time only
!             call ddct2d(n1max, n1, n2, 1, a, t, ip, w)
!         <case2>
!             ip(0) = 0  ! first time only
!             call ddct2d(n1max, n1, n2, -1, a, t, ip, w)
!     [parameters]
!         n1max  :row size of the 2D array (integer)
!         n1     :data length (integer)
!                 n1 >= 2, n1 = power of 2
!         n2     :data length (integer)
!                 n2 >= 2, n2 = power of 2
!         a(0:n1-1,0:n2-1)
!                :input/output data (real*8)
!                 output data
!                     a(k1,k2) = C(k1,k2), 0<=k1<n1, 0<=k2<n2
!         t(0:n2-1)
!                :work area (real*8)
!         ip(0:*):work area for bit reversal (integer)
!                 length of ip >= 2+sqrt(n)  ; if mod(n,4) = 0
!                                 2+sqrt(n/2); otherwise
!                 (n = max(n1/2, n2/2))
!                 ip(0),ip(1) are pointers of the cos/sin table.
!         w(0:*) :cos/sin table (real*8)
!                 length of w >= max(n1*5/4, n2*5/4)
!                 w(),ip() are initialized if ip(0) = 0.
!     [remark]
!         Inverse of 
!             call ddct2d(n1max, n1, n2, -1, a, t, ip, w)
!         is 
!             do j1 = 0, n1 - 1
!                 a(j1, 0) = a(j1, 0) * 0.5d0
!             end do
!             do j2 = 0, n2 - 1
!                 a(0, j2) = a(0, j2) * 0.5d0
!             end do
!             call ddct2d(n1max, n1, n2, 1, a, t, ip, w)
!             do j2 = 0, n2 - 1
!                 do j1 = 0, n1 - 1
!                     a(j1, j2) = a(j1, j2) * (4.0d0 / (n1 * n2))
!                 end do
!             end do
!         .
!
!
! -------- DST (Discrete Sine Transform) / Inverse of DST --------
!     [definition]
!         <case1> IDST (excluding scale)
!             S(k1,k2) = sum_j1=1^n1 sum_j2=1^n2 A(j1,j2) * 
!                            sin(pi*j1*(k1+1/2)/n1) * 
!                            sin(pi*j2*(k2+1/2)/n2), 
!                            0<=k1<n1, 0<=k2<n2
!         <case2> DST
!             S(k1,k2) = sum_j1=0^n1-1 sum_j2=0^n2-1 a(j1,j2) * 
!                            sin(pi*(j1+1/2)*k1/n1) * 
!                            sin(pi*(j2+1/2)*k2/n2), 
!                            0<k1<=n1, 0<k2<=n2
!     [usage]
!         <case1>
!             ip(0) = 0  ! first time only
!             call ddst2d(n1max, n1, n2, 1, a, t, ip, w)
!         <case2>
!             ip(0) = 0  ! first time only
!             call ddst2d(n1max, n1, n2, -1, a, t, ip, w)
!     [parameters]
!         n1max  :row size of the 2D array (integer)
!         n1     :data length (integer)
!                 n1 >= 2, n1 = power of 2
!         n2     :data length (integer)
!                 n2 >= 2, n2 = power of 2
!         a(0:n1-1,0:n2-1)
!                :input/output data (real*8)
!                 <case1>
!                     input data
!                         a(j1,j2) = A(j1,j2), 0<j1<n1, 0<j2<n2, 
!                         a(j1,0) = A(j1,n2), 0<j1<n1, 
!                         a(0,j2) = A(n1,j2), 0<j2<n2, 
!                         a(0,0) = A(n1,n2)
!                         (i.e. A(j1,j2) = a(mod(j1,n1),mod(j2,n2)))
!                     output data
!                         a(k1,k2) = S(k1,k2), 0<=k1<n1, 0<=k2<n2
!                 <case2>
!                     output data
!                         a(k1,k2) = S(k1,k2), 0<k1<n1, 0<k2<n2, 
!                         a(k1,0) = S(k1,n2), 0<k1<n1, 
!                         a(0,k2) = S(n1,k2), 0<k2<n2, 
!                         a(0,0) = S(n1,n2)
!                         (i.e. S(k1,k2) = a(mod(k1,n1),mod(k2,n2)))
!         t(0:n2-1)
!                :work area (real*8)
!         ip(0:*):work area for bit reversal (integer)
!                 length of ip >= 2+sqrt(n)  ; if mod(n,4) = 0
!                                 2+sqrt(n/2); otherwise
!                 (n = max(n1/2, n2/2))
!                 ip(0),ip(1) are pointers of the cos/sin table.
!         w(0:*) :cos/sin table (real*8)
!                 length of w >= max(n1*5/4, n2*5/4)
!                 w(),ip() are initialized if ip(0) = 0.
!     [remark]
!         Inverse of 
!             call ddst2d(n1max, n1, n2, -1, a, t, ip, w)
!         is 
!             do j1 = 0, n1 - 1
!                 a(j1, 0) = a(j1, 0) * 0.5d0
!             end do
!             do j2 = 0, n2 - 1
!                 a(0, j2) = a(0, j2) * 0.5d0
!             end do
!             call ddst2d(n1max, n1, n2, 1, a, t, ip, w)
!             do j2 = 0, n2 - 1
!                 do j1 = 0, n1 - 1
!                     a(j1, j2) = a(j1, j2) * (4.0d0 / (n1 * n2))
!                 end do
!             end do
!         .
!
!
      subroutine cdft2d(n1max, n1, n2, isgn, a, t, ip, w)
      integer n1max, n1, n2, isgn, ip(0 : *), n, i, j, j2
      real*8 a(0 : n1max - 1, 0 : n2 - 1), t(0 : 2 * n2 - 1), 
     &    w(0 : *)
      n = max(n1, 2 * n2)
      if (n .gt. 4 * ip(0)) then
          call makewt(n / 4, ip, w)
      end if
      do j = 0, n2 - 1
          call cdft(n1, isgn, a(0, j), ip, w)
      end do
      do i = 0, n1 - 2, 2
          do j = 0, n2 - 1
              j2 = 2 * j
              t(j2) = a(i, j)
              t(j2 + 1) = a(i + 1, j)
          end do
          call cdft(2 * n2, isgn, t, ip, w)
          do j = 0, n2 - 1
              j2 = 2 * j
              a(i, j) = t(j2)
              a(i + 1, j) = t(j2 + 1)
          end do
      end do
      end
!
      subroutine rdft2d(n1max, n1, n2, isgn, a, t, ip, w)
      integer n1max, n1, n2, isgn, ip(0 : *), n, nw, nc, 
     &    n2h, i, j, j2
      real*8 a(0 : n1max - 1, 0 : n2 - 1), t(0 : 2 * n2 - 1), 
     &    w(0 : *), xi
      n = max(n1, 2 * n2)
      nw = ip(0)
      if (n .gt. 4 * nw) then
          nw = n / 4
          call makewt(nw, ip, w)
      end if
      nc = ip(1)
      if (n1 .gt. 4 * nc) then
          nc = n1 / 4
          call makect(nc, ip, w(nw))
      end if
      n2h = n2 / 2
      if (isgn .lt. 0) then
          do i = 1, n2h - 1
              j = n2 - i
              xi = a(0, i) - a(0, j)
              a(0, i) = a(0, i) + a(0, j)
              a(0, j) = xi
              xi = a(1, j) - a(1, i)
              a(1, i) = a(1, i) + a(1, j)
              a(1, j) = xi
          end do
          do i = 0, n1 - 2, 2
              do j = 0, n2 - 1
                  j2 = 2 * j
                  t(j2) = a(i, j)
                  t(j2 + 1) = a(i + 1, j)
              end do
              call cdft(2 * n2, isgn, t, ip, w)
              do j = 0, n2 - 1
                  j2 = 2 * j
                  a(i, j) = t(j2)
                  a(i + 1, j) = t(j2 + 1)
              end do
          end do
          do j = 0, n2 - 1
              call rdft(n1, isgn, a(0, j), ip, w)
          end do
      else
          do j = 0, n2 - 1
              call rdft(n1, isgn, a(0, j), ip, w)
          end do
          do i = 0, n1 - 2, 2
              do j = 0, n2 - 1
                  j2 = 2 * j
                  t(j2) = a(i, j)
                  t(j2 + 1) = a(i + 1, j)
              end do
              call cdft(2 * n2, isgn, t, ip, w)
              do j = 0, n2 - 1
                  j2 = 2 * j
                  a(i, j) = t(j2)
                  a(i + 1, j) = t(j2 + 1)
              end do
          end do
          do i = 1, n2h - 1
              j = n2 - i
              a(0, j) = 0.5d0 * (a(0, i) - a(0, j))
              a(0, i) = a(0, i) - a(0, j)
              a(1, j) = 0.5d0 * (a(1, i) + a(1, j))
              a(1, i) = a(1, i) - a(1, j)
          end do
      end if
      end
!
      subroutine ddct2d(n1max, n1, n2, isgn, a, t, ip, w)
      integer n1max, n1, n2, isgn, ip(0 : *), n, nw, nc, i, j
      real*8 a(0 : n1max - 1, 0 : n2 - 1), t(0 : n2 - 1), w(0 : *)
      n = max(n1, n2)
      nw = ip(0)
      if (n .gt. 4 * nw) then
          nw = n / 4
          call makewt(nw, ip, w)
      end if
      nc = ip(1)
      if (n .gt. nc) then
          nc = n
          call makect(nc, ip, w(nw))
      end if
      do j = 0, n2 - 1
          call ddct(n1, isgn, a(0, j), ip, w)
      end do
      do i = 0, n1 - 1
          do j = 0, n2 - 1
              t(j) = a(i, j)
          end do
          call ddct(n2, isgn, t, ip, w)
          do j = 0, n2 - 1
              a(i, j) = t(j)
          end do
      end do
      end
!
      subroutine ddst2d(n1max, n1, n2, isgn, a, t, ip, w)
      integer n1max, n1, n2, isgn, ip(0 : *), n, nw, nc, i, j
      real*8 a(0 : n1max - 1, 0 : n2 - 1), t(0 : n2 - 1), w(0 : *)
      n = max(n1, n2)
      nw = ip(0)
      if (n .gt. 4 * nw) then
          nw = n / 4
          call makewt(nw, ip, w)
      end if
      nc = ip(1)
      if (n .gt. nc) then
          nc = n
          call makect(nc, ip, w(nw))
      end if
      do j = 0, n2 - 1
          call ddst(n1, isgn, a(0, j), ip, w)
      end do
      do i = 0, n1 - 1
          do j = 0, n2 - 1
              t(j) = a(i, j)
          end do
          call ddst(n2, isgn, t, ip, w)
          do j = 0, n2 - 1
              a(i, j) = t(j)
          end do
      end do
      end
!
