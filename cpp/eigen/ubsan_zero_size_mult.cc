// From Mmanu: https://gist.github.com/m-chaturvedi/aed94a7fa4b51d5bfcf8ea5f063790dc
/*
clang++-3.9 -std=c++11 -I /usr/include/eigen3 -fsanitize=undefined -fno-sanitize-recover=undefined -fsanitize-trap=undefined ubsan_zero_size_mult.cc && gdb -ex run ./a.out
*/
#include <bits/stdc++.h>
#include <Eigen/Core>
#include <Eigen/Cholesky>
using namespace std;
using namespace Eigen;

int main() {
  MatrixXd A(2, 2);
  A << 1, 0, 0, 1;
  MatrixXd B = MatrixXd::Zero(2,0);
  LLT<MatrixXd> llt(A);
  MatrixXd X = llt.solve(B);
}
