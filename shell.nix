{ pkgs ? import <nixpkgs> { } }:

import ./nix/dev-shell.nix { inherit pkgs; }
