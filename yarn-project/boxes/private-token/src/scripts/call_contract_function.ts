import { AztecAddress, CompleteAddress, Contract, ContractAbi, PXE } from '@aztec/aztec.js';
import { getWallet } from './util.js';

export async function callContractFunction(
  address: AztecAddress,
  abi: ContractAbi,
  functionName: string,
  typedArgs: any[], // for the exposed functions, this is an array of field elements Fr[]
  pxe: PXE,
  wallet: CompleteAddress,
) {
  // selectedWallet is how we specify the "sender" of the transaction
  const selectedWallet = await getWallet(wallet, pxe);

  // TODO: switch to the generated typescript class?
  const contract = await Contract.at(address, abi, selectedWallet);

  return contract.methods[functionName](...typedArgs)
    .send()
    .wait();
}
