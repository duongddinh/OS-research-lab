{
  description = "JordyOS and Apelang development tools";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/d57af924f160a5084293c71c2043f058bd1cdb60";
  outputs = { self, nixpkgs }: let
    systems = [ "aarch64-darwin" "x86_64-darwin" "x86_64-linux" "aarch64-linux" ];
  in { devShells = nixpkgs.lib.genAttrs systems (system: let
    pkgs = import nixpkgs { inherit system; };
  in { default = pkgs.mkShell {
    packages = with pkgs; [ clang llvm lld nasm qemu python3 gnumake git ];
    JORDY_CC = "${pkgs.llvmPackages.clang-unwrapped}/bin/clang";
    shellHook = "export CC=clang";
  }; }); };
}
