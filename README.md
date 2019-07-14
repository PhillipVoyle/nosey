# nosey
A logging TCP proxy

## What is this?
From time to time in my career I've had to do some network programming against a socket protocol I haven't understood, and one of the first things I've usually done to understand that protocol is write a client or server for that protocol. As soon as you do this kind of thing you want to know what your new test app is sending where, and whether it's coming back as you expected. This app is an attempt to get some of those peices of work out of the way next time. The nonblocking I/O implementation could also form the base for the network code.

## How to use this app
This app is really intended as a palette of parts that you could use in a TCP client or server app, however it also works as a tool. It's largely for my own purposes, but you should feel free to edit and use it, and yes I'd love to hear suggestions about how to improve it. Submit an issue or a pull request.

## Usage
<pre>
usage:
        nosey [options]

option listing:
        -l/--listen-addr, 0.0.0.0
        -p/--listen-port, 8080
        -a/--destination-addr, 127.0.0.10
        -d/--destination-port, 80
        -w/--report-width, 8
        -r/--report-repeats, 3
        -t/--report-time
        -i/--report-ip
        -n/--report-port
        -v/--verbose
        -?/--help
</pre>

## Illustrative example

Here's an example connecting to www.example.com via a dumb proxy connected on localhost. Note the failure due to the wrong hostname.

<pre>
./nosey -p 80 -w 16 -r 1 -a 93.184.216.34 -v
configured far end: 93.184.216.34:80
2019-07-14 14:20:54+1200>0.0.0.0:80 listening
2019-07-14 14:21:02+1200>127.0.0.1:50004 accepted connection
2019-07-14 14:21:02+1200>93.184.216.34:80 connecting ...
2019-07-14 14:21:02+1200>127.0.0.1:50004 474554202f20485454502f312e310d0a GET / HTTP/1.1..
2019-07-14 14:21:02+1200>127.0.0.1:50004 486f73743a206c6f63616c686f73740d Host: localhost.
2019-07-14 14:21:02+1200>127.0.0.1:50004 0a436f6e6e656374696f6e3a206b6565 .Connection: kee
2019-07-14 14:21:02+1200>127.0.0.1:50004 702d616c6976650d0a55706772616465 p-alive..Upgrade
2019-07-14 14:21:02+1200>127.0.0.1:50004 2d496e7365637572652d526571756573 -Insecure-Reques
2019-07-14 14:21:02+1200>127.0.0.1:50004 74733a20310d0a557365722d4167656e ts: 1..User-Agen
2019-07-14 14:21:02+1200>127.0.0.1:50004 743a204d6f7a696c6c612f352e302028 t: Mozilla/5.0 (
2019-07-14 14:21:02+1200>127.0.0.1:50004 57696e646f7773204e542031302e303b Windows NT 10.0;
2019-07-14 14:21:02+1200>127.0.0.1:50004 2057696e36343b207836342920417070  Win64; x64) App
2019-07-14 14:21:02+1200>127.0.0.1:50004 6c655765624b69742f3533372e333620 leWebKit/537.36
2019-07-14 14:21:02+1200>127.0.0.1:50004 284b48544d4c2c206c696b6520476563 (KHTML, like Gec
2019-07-14 14:21:02+1200>127.0.0.1:50004 6b6f29204368726f6d652f37352e302e ko) Chrome/75.0.
2019-07-14 14:21:02+1200>127.0.0.1:50004 333737302e313030205361666172692f 3770.100 Safari/
2019-07-14 14:21:02+1200>127.0.0.1:50004 3533372e33360d0a4163636570743a20 537.36..Accept:
2019-07-14 14:21:02+1200>127.0.0.1:50004 746578742f68746d6c2c6170706c6963 text/html,applic
2019-07-14 14:21:02+1200>127.0.0.1:50004 6174696f6e2f7868746d6c2b786d6c2c ation/xhtml+xml,
2019-07-14 14:21:02+1200>127.0.0.1:50004 6170706c69636174696f6e2f786d6c3b application/xml;
2019-07-14 14:21:02+1200>127.0.0.1:50004 713d302e392c696d6167652f77656270 q=0.9,image/webp
2019-07-14 14:21:02+1200>127.0.0.1:50004 2c696d6167652f61706e672c2a2f2a3b ,image/apng,*/*;
2019-07-14 14:21:02+1200>127.0.0.1:50004 713d302e382c6170706c69636174696f q=0.8,applicatio
2019-07-14 14:21:02+1200>127.0.0.1:50004 6e2f7369676e65642d65786368616e67 n/signed-exchang
2019-07-14 14:21:02+1200>127.0.0.1:50004 653b763d62330d0a4163636570742d45 e;v=b3..Accept-E
2019-07-14 14:21:02+1200>127.0.0.1:50004 6e636f64696e673a20677a69702c2064 ncoding: gzip, d
2019-07-14 14:21:02+1200>127.0.0.1:50004 65666c6174652c2062720d0a41636365 eflate, br..Acce
2019-07-14 14:21:02+1200>127.0.0.1:50004 70742d4c616e67756167653a20656e2d pt-Language: en-
2019-07-14 14:21:02+1200>127.0.0.1:50004 55532c656e3b713d302e390d0a0d0a   US,en;q=0.9....
2019-07-14 14:21:03+1200>93.184.216.34:80 connected successfully
2019-07-14 14:21:03+1200<93.184.216.34:80 485454502f312e3120343034204e6f74 HTTP/1.1 404 Not
2019-07-14 14:21:03+1200<93.184.216.34:80 20466f756e640d0a436f6e74656e742d  Found..Content-
2019-07-14 14:21:03+1200<93.184.216.34:80 547970653a20746578742f68746d6c0d Type: text/html.

...

2019-07-14 14:21:45+1200>127.0.0.1:50004 disconnect
2019-07-14 14:21:45+1200>93.184.216.34:80 disconnect
</pre>

## Anything else?

Have a question, or an improvement? Found a bug? This code is currently hosted on github at https://www.github.com/PhillipVoyle/nosey, submit an issue or a pull request, and I'll get to it one day.
