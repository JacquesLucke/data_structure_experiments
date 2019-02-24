from pprint import pprint
import random

mersenne_prime_exponents = [
    2, 3, 5, 7, 13, 17, 19, 31, 61, 89
]


def build_low_high_masks(exp):
    low_mask = 2 ** exp - 1
    high_mask = 2 ** (exp * 2) - 1 - low_mask
    return low_mask, high_mask


class HashFunctionBase:
    exp: int
    m: int
    n: int
    prime: int

    high_mask: int
    low_mask: int
    final_mask: int

    def compute_hash(self, value):
        x = self.m * value + self.n
        x1 = (x & self.high_mask) >> self.exp
        x2 = x & self.low_mask
        s = x1 + x2
        if s > self.prime:
            s -= self.prime

        return s & self.final_mask


class HashFunction_uint8(HashFunctionBase):
    exp = 13
    prime = 2 ** exp - 1
    low_mask, high_mask = build_low_high_masks(exp)

    def __init__(self, required_bits):
        assert 0 <= required_bits < 8
        self.m = random.randint(1, self.prime - 1)
        self.n = random.randint(0, self.prime - 1)
        self.final_mask = 2 ** required_bits - 1

    def __call__(self, value):
        return self.compute_hash(value)


class HashFunction_uint16(HashFunctionBase):
    exp = 17
    prime = 2 ** exp - 1
    low_mask, high_mask = build_low_high_masks(exp)

    def __init__(self, required_bits):
        assert 0 <= required_bits < 16
        self.m = random.randint(1, self.prime - 1)
        self.n = random.randint(0, self.prime - 1)
        self.final_mask = 2 ** required_bits - 1

    def __call__(self, value):
        return self.compute_hash(value)


class HashFunction_uint32(HashFunctionBase):
    exp = 31
    prime = 2 ** exp - 1
    low_mask, high_mask = build_low_high_masks(exp)

    def __init__(self, required_bits):
        assert 0 <= required_bits < 32
        self.m = random.randint(1, self.prime - 1)
        self.n = random.randint(0, self.prime - 1)
        self.final_mask = 2 ** required_bits - 1

    def __call__(self, value):
        # ignores highest bit
        return self.compute_hash(value & self.prime)


values = [4, 123, 65, 3456, 7645, 1231546, 7647, 12]

for _ in range(100000):
    hashfn = HashFunction_uint32(3)
    results = [hashfn(i) for i in values]
    collisions = len(results) - len(set(results))
    if collisions == 0:
        print(hashfn.m, hashfn.n)
        print(results)
