{
  stdenvNoCC,
  as,
  binutils,
  gcc,
  lib,
  lpl-kernel,
  ...
}:
stdenvNoCC.mkDerivation (finalAttrs: {
  pname = "libc";
  inherit (lpl-kernel) version;
  src = ../libc;

  env = {
    HOST = "i686-elf";
    PREFIX = "${placeholder "out"}";
    CC = "${lib.getExe gcc}";
    AR = "${lib.getExe' binutils "i686-elf-ar"}";
    CFLAGS = "-isystem ${lpl-kernel.src}/include";
  };

  nativeBuildInputs = [
    as
    gcc
    binutils
  ];

  preBuild = ''
    substituteInPlace Makefile \
      --replace-fail 'DEFAULT_HOST!=../default-host.sh' "DEFAULT_HOST:=i686-elf" \
      --replace-fail 'HOSTARCH!=../target-triplet-to-arch.sh $(HOST)' "HOSTARCH:=i386"
  '';

  meta = {
    description = "C libary for the lpl kernel";
    inherit (lpl-kernel.meta) license;
  };
})
