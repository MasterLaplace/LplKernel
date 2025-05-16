{
  stdenvNoCC,
  binutils,
  lib,
  ...
}:
stdenvNoCC.mkDerivation {
  pname = "as";
  inherit (binutils) version;

  dontUnpack = true;
  dontBuild = true;

  postInstall = ''
    mkdir -p $out/bin
    cp ${lib.getExe' binutils "i686-elf-as"} $out/bin/as
  '';

  meta = {
    description = "GNU assembler";
    license = lib.licenses.gpl3Plus;
  };
}
