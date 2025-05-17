{
  stdenvNoCC,
  lpl-kernel,
  grub2,
  libisoburn,
  ...
}:
stdenvNoCC.mkDerivation (finalAttrs: {
  pname = "libc";
  inherit (lpl-kernel) version;
  src = ./..;

  nativeBuildInputs = [
    lpl-kernel
    grub2
    libisoburn
  ];

  dontBuild = true;

  installPhase = ''
    mkdir -p $out
    mkdir -p iso/boot/grub

    cat > iso/boot/grub/grub.cfg << EOF
      set timeout=0
      set default=0

      menuentry "lpl" {
        multiboot /boot/lpl.kernel
        boot
      }
    EOF

    cp ${lpl-kernel}/boot/lpl.kernel iso/boot/lpl.kernel
    grub-mkrescue -o $out/lpl.iso iso
  '';

  inherit (lpl-kernel) meta;
})
