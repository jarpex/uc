LLM models leave a lot of typographic garbage and hallucinate Unicode in the middle of text.
`uc` is a primitive filter to show or clean it.

Input: `UTF-8`.  
Output: `UTF-8`.

## Install via Homebrew

```bash
brew install --HEAD jarpex/formulae/uc
```

## Build

```sh
make
```

or:

```sh
cc -std=c99 -Wall -Wextra -O2 -o uc uc.c main.c
```

## Usage

```text
usage: uc [-h] [-v] [-s] [-c] [-e encoding] [--check] [--] [text...]

options:
  -h, --help       show help
  -v, --version    show version
  -s, --show       show problematic characters as [U+XXXX]
  -c, --clear      clean output text
  -e, --encoding   target encoding: ascii, cp1252, cp1251
      --check      exit code 1 if problems were found

-e selects allowed character set; output remains UTF-8.
```

## Example

The input contains invisible `U+00A0` characters:

```sh
uc -s "The Static Linking Illusion: How glibc NSS Shatters Isolation in OT/ICS (and Where Security by Subtraction Fits In)"
```

Output:

```text
The Static Linking Illusion: How glibc NSS Shatters Isolation in[U+00A0]OT/ICS (and Where Security[U+00A0]by[U+00A0]Subtraction Fits[U+00A0]In)
```

## Test

```sh
make test
```
