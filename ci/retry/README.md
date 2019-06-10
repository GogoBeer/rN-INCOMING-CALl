retry - The command line retry tool
------------------------------------------

Retry any shell command with exponential backoff or constant delay.

### Instructions

Install:

retry is a shell script, so drop it somewhere and make sure it's added to your $PATH. Or you can use the following one-liner:

```sh
sudo sh -c "curl https://raw.githubusercontent.com/kadwanev/retry/master/retry -o /usr/local/bin/retry && chmod +x /usr/local/bin/retry"
```

If you're on OS X, retry is also on Homebrew:

```
brew pull 27283
brew install retry
```
Not popular enough for homebrew-core. Please star this project to help.

### Usage

Help:

`retry -?`

    Usage: retry [options] -- execute command
        -h, -?, --help
        -v, --verbose                    Verbose output
        -t, --tries=#                    Set max retries: Default 10
        -s, --sleep=secs                 Constant sleep amount (seconds)
        -m, --min=secs                   Exponential Backoff: minimum sleep amount (seconds): Default 0.3
        -x, --max=secs                   Exponential Backoff: maximum sleep amount (seconds): Default 60
        -f, --fail="script +cmds"        Fail Script: run in case of final failure

### Examples

No problem:

`retry echo u work good`

    u work good

Test functionality:

`retry 'echo "y u no work"; false'`

    y u no work
    Before retry #1: sleeping 0.3 seconds
    y u no work
    Before retry #2: sleeping 0.6 seconds
    y u no work
    Before retry #3: sleeping 1.2 seconds
    y u no work
    Before retry #4: sleeping 2.4 seconds
    y u no work
    Before retry #5: sleeping 4.8 seconds
    y u no work
    Before retry #6: sleeping 9.6 seconds
    y u no work
    Before retry #7: sleeping 19.2 seconds
    y u no work
    Before retry #8: sleeping 38.4 seconds
    y u no work
    Before retry #9: sleeping 60.0 seconds
    y u no work
    Before retry #10: sleeping 60.0 seconds
    y u no work
    etc..

Limit retries:

`retry -t 4 '