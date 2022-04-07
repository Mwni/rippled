import { connect, useTestWallet, advanceLedger } from './common.js'
import log from './log.js'


const { userClient, adminClient } = await connect()
const wallets = []
const num = 1000


log.config({name: 'fillBook'})
log.info(`will create`, num, `wallets`)

const issuer = await useTestWallet(0)

for(let i=0; i<num; i++){
	wallets.push(await useTestWallet(i + 1))
}

await advanceLedger()




log.info(`will create`, num, `offers`)

for(let i=0; i<num; i++){
	let wallet = wallets[i]

	let { result: {offers} } = await userClient.request({
		command: 'account_offers',
		account: wallet.address
	})

	if(offers.length > 0){
		log.info(`trader #${i} (${wallet.address}) created offer already`)
		continue
	}

	let gets = 100 + Math.round(Math.random() * 900)
	let pays = 100 + Math.round(Math.random() * 900)

	let { result } = await adminClient.submit(
		{
			TransactionType: 'OfferCreate',
			Account: wallet.address,
			TakerGets: (gets * 1000000).toString(),
			TakerPays: {
				currency: 'XAU',
				issuer: issuer.address,
				value: pays.toString()
			},
		},
		{
			autofill: true,
			wallet
		}
	)

	if(result.engine_result === 'tesSUCCESS'){
		log.info(`trader #${i} (${wallet.address}) created offer (${pays} XRP for ${gets} XAU)`)
	}
}

await advanceLedger()

log.info(`all done`)
process.exit(0)