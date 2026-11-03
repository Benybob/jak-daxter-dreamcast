"""Prove codec checks reject temporary source mutations. (AI-assisted), ISC.

Each case gets its own disposable module and test copy. Production files are
read only. A mutation counts as killed only by a unittest assertion failure,
never a syntax error, import error, timeout, or unrelated interpreter failure.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


PORT = Path(__file__).resolve().parents[1]


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('mutation anchor is not unique: ' + old)
    return source.replace(old, new, 1)


def main():
    source = (PORT / 'vag_adpcm.py').read_text()
    tests = (PORT / 'tests/test_vag_adpcm.py').read_text()
    cases = [
        ('baseline', None, None, None),
        ('missing_difference_cap', 'min(32767, abs(_AICA_DIFF[nibble]) * state.quant // 8)',
         'abs(_AICA_DIFF[nibble]) * state.quant // 8', None),
        ('signed_arithmetic_divide', 'delta = -magnitude if nibble & 8 else magnitude',
         'delta = (_AICA_DIFF[nibble] * state.quant) >> 3', None),
        ('negative_rail_shared_with_oracle',
         'state.sample = _clamp_s16(state.sample + delta)',
         'state.sample = max(0, _clamp_s16(state.sample + delta))',
         ('max(-32768, sample + (distance if code < 8 else -distance))',
          'max(0, sample + (distance if code < 8 else -distance))')),
        ('repeat_on_any_block', 'if (flags & 0x01) and loop_end is None:',
         'if flags & 0x02:\n            loop_repeat = True\n        if (flags & 0x01) and loop_end is None:', None),
        ('end_always_repeats', 'loop_repeat = bool(flags & 0x02)',
         'loop_repeat = True', None),
        ('reset_jump_history', 'hist1, hist2 = initial_history', 'hist1, hist2 = (0, 0)', None),
        ('combined_spu_prediction',
         'sample += (coef0 * hist1) >> 6\n        sample += (coef1 * hist2) >> 6',
         'sample += (coef0 * hist1 + coef1 * hist2 + 32) >> 6', None),
        ('wrong_spu_coefficient', '(115, -52)', '(114, -52)', None),
        ('wrong_aica_scale', '0x0E6, 0x0E6, 0x0E6, 0x0E6, 0x133, 0x199, 0x200, 0x266,\n',
         '0x0E6, 0x0E6, 0x0E6, 0x0E6, 0x133, 0x199, 0x200, 0x267,\n', None),
        ('high_nibble_first', 'for nibble in (byte & 0xF, (byte >> 4) & 0xF):',
         'for nibble in ((byte >> 4) & 0xF, byte & 0xF):', None),
        ('pad_is_f', 'out.append(low)', 'out.append(low | 0xF0)', None),
        ('constant_silence', 'encoded = encode_aica(vag.samples, state)',
         'encoded = encode_aica([100] * len(vag.samples), state)', None),
        ('encoder_zero_bytes_round_trip', 'return bytes(out)',
         'return bytes(len(out))', None),
        ('encoder_wrong_sign_round_trip', 'error = produced - target',
         'error = produced + target', None),
    ]
    records = []
    with tempfile.TemporaryDirectory(prefix='vag-mutations-') as temporary:
        root = Path(temporary)
        (root / 'tests').mkdir()
        for name, old, new, oracle in cases:
            # Scale factors occur for both signs; mutate both together.
            changed = (source.replace(old, new) if name == 'wrong_aica_scale'
                       else replace_once(source, old, new) if old else source)
            changed_tests = replace_once(tests, *oracle) if oracle else tests
            (root / 'vag_adpcm.py').write_text(changed)
            test = root / 'tests/test_vag_adpcm.py'
            test.write_text(changed_tests)
            result = subprocess.run([sys.executable, '-B', str(test)],
                                    capture_output=True, text=True, timeout=30)
            expected = 0 if name == 'baseline' else 1
            if result.returncode != expected or (expected and
                    ('FAIL:' not in result.stderr or 'ERROR:' in result.stderr)):
                raise RuntimeError(name + '\n' + result.stdout + result.stderr)
            records.append(dict(name=name, exit=result.returncode,
                                source_sha256=hashlib.sha256(changed.encode()).hexdigest(),
                                assertion_failures=result.stderr.count('FAIL:')))
            print(f'{name}: exit {result.returncode}, assertions {records[-1]["assertion_failures"]}')
    output = PORT / 'build/sound-validation'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'codec-mutations.json').write_text(json.dumps(
        dict(ai_assisted=True, license='ISC', cases=records), indent=2) + '\n')


if __name__ == '__main__':
    main()
