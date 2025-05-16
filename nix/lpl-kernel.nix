{
  stdenvNoCC,
  as,
  binutils,
  gcc,
  libc,
  grub2,
  lib,
  ...
}:
stdenvNoCC.mkDerivation (finalAttrs: {
  pname = "lpl-kernel";
  version = "dev";
  src = ../kernel;

  env = {
    HOST = "i686-elf";
    PREFIX = "${placeholder "out"}";
    CC = "${lib.getExe gcc}";
    AR = "${lib.getExe' binutils "i686-elf-ar"}";
    CFLAGS = "-isystem ${libc.src}/include";
    LDFLAGS = "-L ${libc}/lib";
  };

  nativeBuildInputs = [
    as
    gcc
    binutils
    grub2
  ];

  preBuild = ''
    substituteInPlace Makefile \
      --replace-fail 'DEFAULT_HOST!=../default-host.sh' "DEFAULT_HOST:=i686-elf" \
      --replace-fail 'HOSTARCH!=../target-triplet-to-arch.sh $(HOST)' "HOSTARCH:=i386"
  '';

  meta = {
    description = "Kernel of the Laplace project";
    license = lib.licenses.gpl3Plus;
  };
})
