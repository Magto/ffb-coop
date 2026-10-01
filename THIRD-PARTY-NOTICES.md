# Third-party notices

`FFB Co-op.exe` compiles in code that is not this repo's own. Each component below keeps its
licence's copyright and permission notice here, verbatim, as that licence asks. A PR that brings in
more code from elsewhere adds a section in the same shape: what it is, where it lives in this repo,
where it came from, and the licence text.

## TweetNaCl 20140427

- In this repo: `third_party/tweetnacl.c`, `third_party/tweetnacl.h`, unmodified (the ed25519 signature
  check, #21; `src/signature.cpp` supplies the `randombytes` it declares and never calls)
- From: <https://tweetnacl.cr.yp.to/software.html>, version 20140427 (sha256 of `tweetnacl.c`
  `02e65bc3013ff2168983365e55906bc783c4c7e0a60d8100f17bb303a17175c4`, of `tweetnacl.h`
  `43f29ad721d9927b747b0100ab4160c119e7bb180c7c98a66e4bf79d31244287`)
- Licence: public domain, by Daniel J. Bernstein, Bernard van Gastel, Wesley Janssen, Tanja Lange,
  Peter Schwabe and Sjaak Smetsers. <https://tweetnacl.cr.yp.to/>: "TweetNaCl is a self-contained
  public-domain C library". No notice is required; this section records where it came from.

## nlohmann/json 3.12.0

- In this repo: `third_party/json.hpp`
- From: <https://github.com/nlohmann/json>, tag `v3.12.0`, `LICENSE.MIT`

```text
MIT License 

Copyright (c) 2013-2025 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Code ported from mewgenics-coop

Not yet present. The PR that ports mewgenics-coop's loader code (`Magto/mewgenics-coop`, MIT,
Magto) adds its section here: the files it came from, the files it went to, and that repo's
`LICENSE` text.
