import random

def run_test(buckets, bucket_size):
    counters = [0] * buckets
    full_counter = 0

    while True:
        index = random.randint(0, buckets - 1)
        if counters[index] == bucket_size:
            full_counter += 1
        counters[index] += 1

        if full_counter > 3:
            break

    return sum(counters)

def get_factor(buckets, bucket_size):
    inserted_elements = run_test(buckets, bucket_size)
    return inserted_elements / (buckets * bucket_size)

def get_factor_stats(buckets, bucket_size):
    N = 10000
    min_fac = min(get_factor(buckets, bucket_size) for _ in range(N))
    max_fac = max(get_factor(buckets, bucket_size) for _ in range(N))
    average_fac = sum(get_factor(buckets, bucket_size) for _ in range(N)) / N
    return min_fac, average_fac, max_fac

from pprint import pprint
pprint(get_factor_stats(100, 16))