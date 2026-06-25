{
  stdenv,
  fetchurl,
  lib,
  ...
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "binutils";
  version = "2.42";

  src = fetchurl {
    url = "https://ftp.gnu.org/gnu/binutils/binutils-${finalAttrs.version}.tar.xz";
    hash = "sha256-9uTUH9X8d4sGt4kUV7NiDaXs6hAGxqSkGumYEJ+FqAA=";
  };

  configureFlags = [
    "--target=i686-elf"
    "--prefix=${placeholder "out"}"
    "--with-sysroot"
    "--disable-nls"
    "--disable-werror"
  ];

  enableParallelBuilding = true;
  hardeningDisable = ["all"];

  checkPhase = ''
    make test
  '';

  meta = {
    description = "System binary utilities (i686-elf cross, C++23 toolchain)";
    license = lib.licenses.gpl3Plus;
  };
})
