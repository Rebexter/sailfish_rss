# SailRSS

A native [FreshRSS](https://freshrss.org/) client for Sailfish OS.

**Status:** early development. You can log in, sync and read your feeds;
images in articles and automatic refresh are still missing. See [docs/PLAN.md](docs/PLAN.md) for the roadmap.

## Requirements

- [Sailfish SDK](https://docs.sailfishos.org/Tools/Sailfish_SDK/) 3.13 or
  newer, with the Docker build engine and the `SailfishOS-5.1.0.11-aarch64`
  target
- The project checked out inside the SDK workspace (by default your home
  directory, configurable in the SDK)
- A Sailfish OS device registered in the SDK (Qt Creator: Options → Devices)
- A FreshRSS server with API access enabled (Administration → Authentication)
  and an API password set in your user profile

## Building and deploying

```sh
scripts/dev.sh build     # build the RPM into build/aarch64/RPMS/, runs unit tests
scripts/dev.sh check     # run the Jolla Store (Harbour) validator
scripts/dev.sh deploy    # install on the device (confirm on the phone)
scripts/dev.sh all       # all three
```

The script defaults to the `SailfishOS-5.1.0.11-aarch64` target and the device
named `Jolla Phone (2026) (ARM 64bit)`. Override them with the `SFDK_TARGET`
and `SFDK_DEVICE` environment variables, and the `sfdk` path with `SFDK`.

After changing user-visible strings, run `scripts/dev.sh lupdate` to refresh
the translation files in `translations/`.

## Testing

`scripts/dev.sh build` runs the unit tests in `tests/` under emulation;
`scripts/dev.sh test` reruns them.

`scripts/dev.sh live-test` runs the API client and sync against a throwaway
FreshRSS in Podman, started and filled with a few feeds by
`scripts/dev.sh test-server`. The tests change read and starred state
freely, so they only ever use this test server, never a real account.

```sh
scripts/dev.sh test-server       # once; listens on localhost and the SDK network
scripts/dev.sh live-test
scripts/dev.sh test-server-stop  # removes it
```

## License

[GPL-3.0-or-later](LICENSE)
