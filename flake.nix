{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = {
    self,
    nixpkgs,
  }: let
    forAllSystems = function:
      nixpkgs.lib.genAttrs [
        "x86_64-linux"
      ] (system: function nixpkgs.legacyPackages.${system});
  in {
    devShells = forAllSystems (pkgs: {
      default = pkgs.mkShell {
        hardeningDisable = ["fortify"];
        packages =
          (builtins.attrValues self.packages.${pkgs.system})
          ++ (with pkgs; [
            grub2
            libisoburn
            qemu
        ]);
      };
    });

    formatter = forAllSystems (pkgs: pkgs.alejandra);

    packages = forAllSystems (pkgs: let
      pkgs' = self.packages.${pkgs.system};
      inherit (pkgs) lib callPackage writeShellScriptBin;
    in {
      binutils = callPackage ./nix/binutils-2-36.nix {};

      # gcc invoke ar through the environment (AR), but has as hardcorded
      as = callPackage ./nix/as.nix pkgs';

      gcc = callPackage ./nix/gcc-10-2-0.nix pkgs';

      libc = callPackage ./nix/libc.nix pkgs';

      lpl-kernel = callPackage ./nix/lpl-kernel.nix pkgs';

      iso = callPackage ./nix/iso.nix pkgs';

      default = pkgs'.run-vm;

      run-vm = writeShellScriptBin "run-vm" ''
        ${lib.getExe pkgs.qemu} -cdrom ${pkgs'.iso}/lpl.iso
      '';
    });
  };
}
