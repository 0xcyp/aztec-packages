from math import log2
import pandas as pd
import numpy as np
from ultra_verification import time_to_prove_ultra, time_to_construct_msm_witnesses_over_BN254

def print_circuit_data(msg, circuit):
    print(msg)
    print(f"  log_n:                     {circuit.log_n}")
    msm_time = time_to_construct_msm_witnesses_over_BN254(circuit)
    print(f"  max memory:                {circuit.max_memory} KiB")
    print(f"  time to consruct verifier: {msm_time} ms")

    print(f"  time to verify:            {time_to_prove_ultra(circuit.log_n)} ms")
    proof_size = circuit.proof_size
    print(f"  proof_size:                {proof_size} B")


class DataLogEntry:
    def __init__(self, log_n, max_memory, msm_time, verifier_time, proof_size):
        self.log_n = log_n
        self.max_memory = round(max_memory / (1 << 20), 1)  # B to MiB
        self.msm_time = round(msm_time / 1000, 1)           # ms to s
        self.verifier_time = round(verifier_time / 1000, 1) # ms to s
        self.proof_size = round(proof_size / (1 << 10), 1)  # B to KiB

    def to_list(self):
        return [self.log_n, self.max_memory, self.msm_time, self.verifier_time, self.proof_size]

class DataLog:
    def __init__(self):
        self.last_stack_circuit_data = []
        self.eccvm_data = []
        self.translator_data = []
        self.log_table = None


    def add_entries(self, stack_circuit, eccvm, translator):
        def ultra_verifier_msm_time(circuit, native_msm):
            if native_msm:
                msm_time = sum([msm.time_ultra_native()
                                for msm in circuit.verifier_msms])
            else:
                msm_time = sum([msm.time_ultra_non_native()
                                for msm in circuit.verifier_msms])
            return int(msm_time)

        entry = DataLogEntry(stack_circuit.log_n, 
                             stack_circuit.max_memory, 
                             ultra_verifier_msm_time(stack_circuit, native_msm=False), 
                             time_to_prove_ultra(stack_circuit.log_n),
                             stack_circuit.proof_size)
        self.last_stack_circuit_data.append(entry.to_list())

        entry = DataLogEntry(eccvm.log_n, 
                             eccvm.max_memory, 
                             ultra_verifier_msm_time(eccvm, native_msm=True), 
                             time_to_prove_ultra(eccvm.log_n),
                             eccvm.proof_size)
        self.eccvm_data.append(entry.to_list())

        entry = DataLogEntry(translator.log_n, 
                             translator.max_memory, 
                             ultra_verifier_msm_time(translator, native_msm=False), 
                             time_to_prove_ultra(translator.log_n),
                             translator.proof_size)
        self.translator_data.append(entry.to_list())

    def process(self):
        NUM_FEATURES = 5
        NUM_PROOF_TYPES = 3  # final Goblin recursion proof, eccvm, translator

        num_log_points = len(self.eccvm_data) # the number of times we log values
        assert (len(self.last_stack_circuit_data) == num_log_points)
        assert (len(self.translator_data) == num_log_points)
        arr = np.array([self.last_stack_circuit_data, 
                        self.eccvm_data, 
                        self.translator_data])

        # reshape so there are three rows, one for each of: final stack circuit; eccvm; translator
        arr = arr.reshape(3, NUM_FEATURES * num_log_points)
        # split every 4 columns (4 being the number of features)
        arr = np.array(np.hsplit(arr, num_log_points))
        # arrange into one row for each log point
        arr = arr.reshape(num_log_points, NUM_FEATURES*NUM_PROOF_TYPES)
        # transpose for compact representation
        arr = arr.transpose()

        # make a data frame for ease of visualization
        df = pd.DataFrame(arr)
        proof_types = ["Last circuit", "ECCVM", "Translator"]
        attribute_strings = ["log(circuit size)", 
                            "max memory                             (MiB)", 
                            "verifier MSM witness construction time (s)", 
                            "verifier proof construction time       (s)", 
                            "proof_size                             (KiB)"]
        index = pd.MultiIndex.from_product([proof_types, attribute_strings])
        df.index = index
        df.columns = [1<<(1+i) for i in range(num_log_points)] # TODO: these should be supplied at log time
        df.columns.names = ["num circuits in stack"]

        self.log_table = df
        
    def print(self):
        self.process()
        print(self.log_table)

def get_log_circuit_size(num_gates):
    return 1 + int(log2(num_gates))


def info(input):
    print(input)