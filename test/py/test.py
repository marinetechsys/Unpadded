from module import *
import pytest

def test_encode_and_decode():
    assert f1.encode() == b'\x00'
    assert f2.encode(0x10) == b'\x01\x10'
    assert f3.encode() == b'\x02'
    assert f4.encode(0x20) == b'\x03\x20'
    assert f5.encode(0x30) == b'\04\x30'
    assert f6.encode(0x40) == b'\x05\x40'

    assert f1.decode(b'\xee') == 0xee
    assert f2.decode(b'') is None
    assert f3.decode(b'') is None
    assert f4.decode(b'\xff') == 0xff
    assert f5.decode(b'') is None
    assert f6.decode(b'') is None
