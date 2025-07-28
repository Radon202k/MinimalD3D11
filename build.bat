@echo off

IF NOT EXIST bin mkdir bin

pushd bin
cl -Z7 -FC -W4 -wd4100 -wd4201 -nologo ../main.c
popd