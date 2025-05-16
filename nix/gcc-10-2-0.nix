{
  stdenv,
  gmp,
  mpfr,
  libmpc,
  fetchzip,
  lib,
  binutils,
  ...
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "gcc";
  version = "10.2.0";

  src = fetchzip {
    url = "http://ftp.gnu.org/gnu/gcc/gcc-${finalAttrs.version}/gcc-${finalAttrs.version}.tar.gz";
    hash = "sha256-xMTbZEEyj0eS3Dkdi6izA88igcSm5arkuM+Xr0xINlA=";
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
    "--enable-languages=c"
    "--without-headers"
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

  makeFlags = [
    "all-gcc"
    "all-target-libgcc"
  ];

  installFlags = [
    "install-gcc"
    "install-target-libgcc"
  ];

  meta = {
    description = "GNU Compiler Collection";
    license = lib.licenses.gpl3Plus;
    mainProgram = "i686-elf-gcc";
  };
})
