{
  stdenv,
  fetchzip,
  lib,
  ...
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "binutils";
  version = "2.36";

  src = fetchzip {
    url = "http://ftp.gnu.org/gnu/binutils/binutils-${finalAttrs.version}.tar.gz";
    hash = "sha256-tSUy7k9CvsMdfzkKXvJYM2OxpipFK0RdUmrIevjsIRI=";
  };

  configureFlags = [
    "--target=i686-elf"
    "--prefix=${placeholder "out"}"
    "--disable-nls"
    "--disable-werror"
  ];

  enableParallelBuilding = true;
  hardeningDisable = ["all"];

  checkPhase = ''
    make test
  '';

  meta = {
    description = "System binary utilities";
    license = lib.licenses.gpl3Plus;
  };
})
