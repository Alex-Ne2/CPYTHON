import unittest
import sys
from threading import Thread, Barrier
from itertools import batched
from test.support import threading_helper


class EnumerateThreading(unittest.TestCase):

    @threading_helper.reap_threads
    @threading_helper.requires_working_threading()
    def test_threading(self):
        number_of_threads = 10
        number_of_iterations = 20
        barrier = Barrier(number_of_threads)
        def work(it):
            barrier.wait()
            while True:
                try:
                    _ = next(it)
                except StopIteration:
                    break

        data = tuple(range(1000))
        for it in range(number_of_iterations):
            batch_iterator = batched(data, 2)
            worker_threads = []
            for ii in range(number_of_threads):
                worker_threads.append(
                    Thread(target=work, args=[batch_iterator]))
            for t in worker_threads:
                t.start()
            for t in worker_threads:
                t.join()

            barrier.reset()

if __name__ == "__main__":
    unittest.main()
