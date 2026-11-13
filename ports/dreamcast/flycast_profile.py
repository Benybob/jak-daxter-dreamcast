"""The console Flycast emulates for a test run (AI-assisted), ISC.

Stock unless the environment says otherwise: 16 MB of RAM and a 200 MHz SH-4,
the console the port has to run on, and the one every suite's counts and
timings are for.

DC_FLYCAST_PROFILE=modded is a console with the 32 MB RAM modification and
the SH-4 clocked at 250 MHz: more room and about a quarter more speed, for
getting a build running before it fits. A frame time or a pass under it says
nothing about stock hardware.
"""
import os

PROFILES = {
    'stock': ('no', 200),
    'modded': ('yes', 250),
}


def name():
    profile = os.environ.get('DC_FLYCAST_PROFILE', 'stock')
    if profile not in PROFILES:
        raise SystemExit(f'DC_FLYCAST_PROFILE must be one of {sorted(PROFILES)}, not {profile!r}')
    return profile


def config():
    """Flycast -config arguments for the profile."""
    ram32, mhz = PROFILES[name()]
    return ['-config', f'config:Dreamcast.RamMod32MB={ram32}', '-config', f'config:Sh4Clock={mhz}']
