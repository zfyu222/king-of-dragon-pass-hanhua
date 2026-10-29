"""Protect opaque byte operands across branch expansion and dictionary export."""
import unittest
from build_runtime_payload import unescape_bytes, escape_bytes, expanded_pairs


class OpalByteTests(unittest.TestCase):
    def test_random_branch_keeps_high_and_nul_operands(self):
        source=r'\x01\xC3\x21 \x02\x02charged/rushed\x02 \x01\xD8\x00.'
        target=r'\x01\xC3\x21\x02\x02冲锋/突袭\x02\x01\xD8\x00。'
        rows=expanded_pairs(unescape_bytes(source),unescape_bytes(target))
        self.assertEqual(len(rows),2)
        for a,b in rows:
            self.assertIn(b'\x01\xc3!',a)
            self.assertIn(b'\x01\xd8\0',b)
            self.assertEqual(unescape_bytes(escape_bytes(a)),a)
            self.assertEqual(unescape_bytes(escape_bytes(b)),b)

    def test_valid_utf8_operand_bytes_stay_identical(self):
        value=unescape_bytes('\\x01Á, ')  # original bytes 01 C3 81 2C
        self.assertEqual(value,b'\x01\xc3\x81, ')
        self.assertEqual(unescape_bytes(escape_bytes(value)),value)

    def test_random_branch_operands_do_not_act_as_delimiters(self):
        source=b'\x02\x02first \x01\xc3\x02/second \x01\x41/\x02.'
        target=b'\x02\x02A \x01\xc3\x02/B \x01\x41/\x02.'
        self.assertEqual(expanded_pairs(source,target),[
            (b'first \x01\xc3\x02.',b'A \x01\xc3\x02.'),
            (b'second \x01\x41/.',b'B \x01\x41/.')])


if __name__=='__main__':unittest.main()
