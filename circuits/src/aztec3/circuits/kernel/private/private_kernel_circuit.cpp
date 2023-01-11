#include "init.hpp"

#include <aztec3/circuits/types/array.hpp>

#include <aztec3/circuits/abis/private_kernel/private_inputs.hpp>
#include <aztec3/circuits/abis/private_kernel/public_inputs.hpp>

namespace aztec3::circuits::kernel::private_kernel {

using aztec3::circuits::abis::private_kernel::PrivateInputs;
using aztec3::circuits::abis::private_kernel::PublicInputs;

using aztec3::circuits::types::array_length;
using aztec3::circuits::types::array_pop;
using aztec3::circuits::types::is_array_empty;
using aztec3::circuits::types::push_array_to_array;

// TODO: NEED TO RECONCILE THE `proof`'s public inputs (which are uint8's) with the
// private_call.call_stack_item.public_inputs!
CT::AggregationObject verify_proofs(Composer& composer,
                                    PrivateInputs<CT> const& private_inputs,
                                    size_t const& num_private_call_public_inputs,
                                    size_t const& num_private_kernel_public_inputs)
{
    CT::AggregationObject aggregation_object = Aggregator::aggregate(
        &composer, private_inputs.private_call.vk, private_inputs.private_call.proof, num_private_call_public_inputs);

    Aggregator::aggregate(&composer,
                          private_inputs.previous_kernel.vk,
                          private_inputs.previous_kernel.proof,
                          num_private_kernel_public_inputs,
                          aggregation_object);

    return aggregation_object;
}

void initialise_end_values(PrivateInputs<CT> const& private_inputs, PublicInputs<CT>& public_inputs)
{
    public_inputs.constants = private_inputs.previous_kernel.public_inputs.constants;

    // Ensure the arrays are the same as previously, before we start pushing more data onto them:
    auto& end = public_inputs.end;
    const auto& start = private_inputs.previous_kernel.public_inputs.end;

    end.output_commitments = start.output_commitments;
    end.input_nullifiers = start.input_nullifiers;

    end.private_call_stack = start.private_call_stack;
    end.public_call_stack = start.public_call_stack;
    end.l1_msg_stack = start.l1_msg_stack;

    end.optionally_revealed_data = start.optionally_revealed_data;
}

void update_end_values(PrivateInputs<CT> const& private_inputs, PublicInputs<CT>& public_inputs)
{
    const auto private_call_public_inputs = private_inputs.private_call.call_stack_item.public_inputs;

    const auto& output_commitments = private_call_public_inputs.output_commitments;
    const auto& input_nullifiers = private_call_public_inputs.input_nullifiers;

    const auto& is_static_call = private_inputs.private_call.call_stack_item.public_inputs.call_context.is_static_call;

    // No state changes are allowed for static calls:
    is_static_call.must_imply(is_array_empty<Composer>(output_commitments) == true);
    is_static_call.must_imply(is_array_empty<Composer>(input_nullifiers) == true);

    const auto& storage_contract_address =
        private_inputs.private_call.call_stack_item.public_inputs.call_context.storage_contract_address;

    { // commitments & nullifiers
        std::array<CT::fr, OUTPUT_COMMITMENTS_LENGTH> siloed_output_commitments;
        for (size_t i = 0; i < output_commitments.size(); ++i) {
            siloed_output_commitments[i] =
                CT::fr::conditional_assign(output_commitments[i] == 0,
                                           0,
                                           CT::compress({ storage_contract_address.to_field(), output_commitments[i] },
                                                        GeneratorIndex::OUTER_COMMITMENT));
        }
        std::array<CT::fr, INPUT_NULLIFIERS_LENGTH> siloed_input_nullifiers;
        for (size_t i = 0; i < input_nullifiers.size(); ++i) {
            siloed_input_nullifiers[i] =
                CT::fr::conditional_assign(input_nullifiers[i] == 0,
                                           0,
                                           CT::compress({ storage_contract_address.to_field(), input_nullifiers[i] },
                                                        GeneratorIndex::OUTER_NULLIFIER));
        }

        push_array_to_array<Composer>(siloed_output_commitments, public_inputs.end.output_commitments);
        push_array_to_array<Composer>(siloed_input_nullifiers, public_inputs.end.input_nullifiers);
    }

    {
        // TODO: we need to pass in UNPACKED stack data. I.e. the preimages of the call_stack_item hashes, so that data
        // in the stack can be validated as being correct. (e.g. call_contexts of calls made by the private_call
        // currently being validated).

        // So we'll need to ensure our test_apps return not only a PrivateCircuitPublicInputs object, but also an object
        // containing a TONNE of preimage data. Stuff like:
        // - Stack item preimages
        // - Commitment and nullifier preimages
        // - Hash paths and leaf indices
        // - Any and all preimage data derived by the circuit or through oracle calls.

        // Update on this topic: I've created call_context_reconciliation_data, which allows the call_context to
        // be efficiently unpacked from a call_stack_item_hash. We'll need some "functions calling functions"
        // tests cases to see how best to move this data around neatly.
    }

    const auto& portal_contract_address = private_inputs.private_call.portal_contract_address;

    {
        const auto& l1_msg_stack = private_call_public_inputs.l1_msg_stack;
        std::array<CT::fr, L1_MSG_STACK_LENGTH> l1_call_stack;

        for (size_t i = 0; i < l1_msg_stack.size(); ++i) {
            l1_call_stack[i] = CT::fr::conditional_assign(
                l1_msg_stack[i] == 0,
                0,
                CT::compress({ portal_contract_address, l1_msg_stack[i] }, GeneratorIndex::L1_CALL_STACK_ITEM));
        }
    }
}

void validate_private_call_hash(PrivateInputs<CT> const& private_inputs)
{
    const auto& start = private_inputs.previous_kernel.public_inputs.end;
    const auto private_call_hash = array_pop<Composer>(start.private_call_stack);
    const auto calculated_private_call_hash = private_inputs.private_call.call_stack_item.hash();

    private_call_hash.assert_equal(calculated_private_call_hash, "private_call_hash does not reconcile");
};

void validate_inputs(PrivateInputs<CT> const& private_inputs)
{

    const auto& next_call = private_inputs.private_call.call_stack_item;

    next_call.function_signature.is_private.assert_equal(
        true, "Cannot execute a non-private function with the private kernel circuit");

    const auto& start = private_inputs.previous_kernel.public_inputs.end;

    const CT::boolean is_base_case = start.private_call_count == 0;
    const CT::boolean is_recursive_case = !is_base_case;

    CT::fr start_private_call_stack_length = array_length<Composer>(start.private_call_stack);
    CT::fr start_public_call_stack_length = array_length<Composer>(start.public_call_stack);
    CT::fr start_l1_msg_stack_length = array_length<Composer>(start.l1_msg_stack);

    // Base Case
    {
        // Validate callstack lengths:
        is_base_case.must_imply(
            start_private_call_stack_length ==
                    1 // TODO: might change to allow 3, so a fee can be paid and a gas rebate can be paid.
                && start_public_call_stack_length == 0 && start_l1_msg_stack_length == 0,
            "Invalid callstacks for base case.");

        is_base_case.must_imply(next_call.public_inputs.call_context.is_delegate_call == false &&
                                    next_call.public_inputs.call_context.is_static_call == false,
                                "A user cannot make a delegatecall or staticcall");

        // The below also prevents delegatecall/staticcall
        is_base_case.must_imply(next_call.public_inputs.call_context.storage_contract_address ==
                                    next_call.contract_address,
                                "Storage contract address must be that of the called contract in the base case");
    }

    // Recursive Case
    {
        is_recursive_case.must_imply(private_inputs.previous_kernel.public_inputs.is_private == true,
                                     "Cannot verify a non-private kernel snark in the private kernel circuit");

        is_recursive_case.must_imply(next_call.function_signature.is_constructor == false,
                                     "A constructor must be executed as the first tx in the recursion");

        is_recursive_case.must_imply(start_private_call_stack_length != 0);
    }

    validate_private_call_hash(private_inputs);
}

// TODO: decide what to return.
// TODO: is there a way to identify whether an input has not been used by ths circuit? This would help us more-safely
// ensure we're constraining everything.
void private_kernel_circuit(Composer& composer, OracleWrapper& oracle, PrivateInputs<NT> const& _private_inputs)
{
    (void)oracle; // To avoid unused variable compiler errors whilst building.

    const PrivateInputs<CT> private_inputs = _private_inputs.to_circuit_type(composer);

    // We'll be pushing data to this during execution of this circuit.
    PublicInputs<CT> public_inputs{};

    // const auto& start = private_inputs.previous_kernel.public_inputs.end;

    // const CT::boolean is_base_case = start.private_call_count == 0;
    // const CT::boolean is_recursive_case = !is_base_case;

    validate_inputs(private_inputs);

    initialise_end_values(private_inputs, public_inputs);

    auto aggregation_object = verify_proofs(composer,
                                            private_inputs,
                                            _private_inputs.private_call.vk->num_public_inputs,
                                            _private_inputs.previous_kernel.vk->num_public_inputs);

    // TODO: kernel vk membership check!

    public_inputs.end.aggregation_object = aggregation_object;

    // public_inputs.set_public();
};

} // namespace aztec3::circuits::kernel::private_kernel