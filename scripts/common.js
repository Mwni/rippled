import xrpl from 'xrpl'
import * as cfg from './cfg.js'
import log from './log.js'
import Decimal from 'decimal.js'

const { Client, Wallet } = xrpl

const userClient = new Client(cfg.userWebsocketUrl)
const adminClient = new Client(cfg.adminWebsocketUrl)
const genesisWallet = Wallet.fromSeed(cfg.genesisSeed)


export async function connect(){
	await userClient.connect()
	log.info(`connected to ${cfg.userWebsocketUrl} (regular)`)

	await adminClient.connect()
	log.info(`connected to ${cfg.adminWebsocketUrl} (admin)`)

	return { userClient, adminClient }
}

export async function advanceLedger(force){
	let { result: {ledger_current_index} } = await adminClient.request({
		command: 'ledger_current'
	})

	let { result: {ledger} } = await adminClient.request({
		command: 'ledger',
		ledger_index: ledger_current_index,
		transactions: true
	})

	if(ledger.transactions.length === 0 && !force){
		log.info(`ledger advance skipped (no new tx)`)
		return
	}

	let { result: {ledger_current_index: newIndex} } = await adminClient.request({
		command: 'ledger_accept',
	})

	log.info(`-> new ledger #${newIndex} with ${ledger.transactions.length} tx`)
}


export async function useTestWallet(index){
	let wallet = Wallet.fromEntropy(
		new Uint8Array(
			Buffer.from(`*** ${index} entropy ***`)
		)
	)

	while(true){
		try{
			let { result } = await adminClient.request({
				command: 'account_info',
				account: wallet.address
			})
	
			log.info(
				`test account #${index+1} (${wallet.address}) has`, 
				Decimal.div(result.account_data.Balance, '1000000').toNumber(), 
				`XRP`
			)
			
			break
		}catch{
			let { result } = await adminClient.submit(
				{
					TransactionType: 'Payment',
					Account: genesisWallet.address,
					Destination: wallet.address,
					Amount: '100000000000',
				}, 
				{
					autofill: true, 
					wallet: genesisWallet
				}
			)
	
			if(result.engine_result !== 'tesSUCCESS')
				throw result.engine_result_message
		}
	}

	return wallet
}