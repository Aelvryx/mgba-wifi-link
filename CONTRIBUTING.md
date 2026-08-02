# Contributing to mGBA Wi-Fi Link

Thanks for helping improve the fork. This project keeps its process small, but
changes should remain reviewable, reproducible, and honest about evidence.

## Choose the right project

- Fork-specific Netpacket, replicated-pair, Android packaging, compatibility,
  or roadmap work belongs here.
- A general mGBA emulator defect that reproduces upstream should normally be
  discussed with [upstream mGBA](https://github.com/mgba-emu/mgba).
- Never send commercial ROMs, BIOS files, copyrighted assets, or private save
  data to either project.

See [SUPPORT.md](SUPPORT.md) when unsure.

## Before coding

Search the issue tracker and roadmap first. Small fixes can go straight to a
focused pull request. Changes to cable semantics, distributed state, persistent
data, wire compatibility, or release policy should begin with a short OpenSpec
change so the invariant is reviewed before implementation.

One issue or change should have one clear outcome. Avoid combining emulator
behaviour, release tooling, and repository administration unless they are
inseparable.

## Branches and pull requests

1. Branch from current `master`.
2. Keep commits coherent and use component-oriented subjects, for example
   `netplay: bound replica readiness wait`.
3. Explain the user-visible result and failure semantics in the pull request.
4. Add focused tests for every corrected invariant.
5. Let required CI pass before merge.
6. Keep generated builds, commercial content, device logs, screenshots, and
   private qualification manifests out of Git.

The repository deletes merged branches automatically. Force-pushes and direct
changes to `master` are blocked by repository policy except for an explicit
maintainer emergency bypass.

## Testing ownership

Testing stays in two clear buckets:

- Automation owns builds, deterministic replay, fault injection, logging,
  analysis, device staging, and cleanup.
- Humans own stock RetroArch host/join interaction, complex game navigation,
  commercial gameplay, and subjective audiovisual or latency judgement.

Do not automate controller input merely to avoid asking for a short human step;
on Android that can alter controller enumeration and invalidate the run.

## Minimum validation

Run the smallest relevant focused tests locally. Pull-request CI supplies the
broader normal, sanitizer, thread-sanitizer, complete-suite, reproducibility,
and Android build gates.

Production SIO, replica, adapter, timing, persistence, or input changes should
also preserve:

- deterministic trace equality;
- transactional state/save rollback;
- generation-safe callback teardown;
- the currently qualified Mario Kart and Four Swords paths.

Commercial device qualification is required only when the change can affect
the exercised behaviour. Documentation and tooling corrections should not
trigger ceremonial replays of unchanged emulator code.

## Coding and licensing

Match the surrounding mGBA style: tabs for indentation, camelCase functions and
variables, leading underscores for file-static functions and variables, and
MPL-2.0-compatible contributions.

AI-assisted contributions are allowed here. The human contributor remains
responsible for understanding the change, licensing its inputs, reviewing the
result, and supplying reproducible tests. Do not present generated text, code,
or evidence as independently verified when it is not.

By contributing, you agree that your contribution is available under the
repository's [MPL-2.0 license](LICENSE).
