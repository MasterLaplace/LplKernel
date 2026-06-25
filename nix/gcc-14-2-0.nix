{
  stdenv,
  gmp,
  mpfr,
  libmpc,
  fetchurl,
  lib,
  binutils,
  ...
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "gcc";
  version = "14.2.0";

  src = fetchurl {
    url = "https://ftp.gnu.org/gnu/gcc/gcc-${finalAttrs.version}/gcc-${finalAttrs.version}.tar.xz";
    hash = "sha256-p7Obxpy/niWCbFpgqyZHcAH3wI2FzsBLwOKcq+1vPMk=";
  };

  preConfigure = ''
    mkdir build
    cd build
  '';

  configureScript = "../configure";

  configureFlags = [
    "--target=i686-elf"
    "--prefix=${placeholder "out"}"
    "--disable-nls"
    # C++23 freestanding engine module (libengine.a) needs the C++ front end.
    "--enable-languages=c,c++"
    "--without-headers"
    # Build the freestanding-mode libstdc++ (P1642 subset): provides
    # <type_traits>/<concepts>/<bit>/<span>/<array>/<optional>/<expected>/<atomic>/
    # <string_view>/<functional>/<memory>... but NOT the hosted containers
    # <vector>/<string>/<unordered_map> (those are hand-rolled allocator-backed in kernel_std).
    "--disable-hosted-libstdcxx"
    "--disable-libssp"
    "--disable-libquadmath"
    "--disable-libcc1"
  ];

  enableParallelBuilding = true;
  hardeningDisable = ["all"];

  nativeBuildInputs = [
    gmp
    mpfr
    libmpc
    binutils
  ];

  # all-target-libstdc++-v3 builds the freestanding libstdc++ described above;
  # without it only gcc's own C headers (stddef.h/stdint.h/intrinsics) are installed
  # and every C++ stdlib #include fails.
  makeFlags = [
    "all-gcc"
    "all-target-libgcc"
    "all-target-libstdc++-v3"
  ];

  installFlags = [
    "install-gcc"
    "install-target-libgcc"
    "install-target-libstdc++-v3"
  ];

  meta = {
    description = "GNU Compiler Collection (i686-elf cross, C++23 freestanding)";
    license = lib.licenses.gpl3Plus;
    mainProgram = "i686-elf-gcc";
  };
})
