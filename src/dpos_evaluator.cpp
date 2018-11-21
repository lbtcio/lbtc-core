
#include "lbtc.pb.h"
#include "dpos_db.h"
#include "dpos_evaluator.h"

bool DposEvaluator::Do(const TxMsg& msg, const std::string& data)
{
	bool ret = false;
	LbtcPbMsg::Msg baseTokenMsg;
	if (baseTokenMsg.ParseFromString(data)) {
		switch(baseTokenMsg.opid()) {
		case DposOpid::REGISTENAME:
			{
			LbtcPbMsg::RegisteNameMsg msgRegisteName;
			if (msgRegisteName.ParseFromString(data)) {
				ret = RegisteName(RegisteNameMsg(msg.nBlockHeight, msg.fee, msg.txHash, msg.fromAddress, msgRegisteName.name()));
			}
			}
		break;
		}
	}

	return ret;
}

bool DposEvaluator::Commit(uint64_t nBlockHeight)
{
	auto& db = *DposDB::GetInstance();
	db.Commit(nBlockHeight);
	return true;
}

bool DposEvaluator::Rollback(uint64_t nBlockHeight)
{
	auto& db = *DposDB::GetInstance();
	db.Rollback(nBlockHeight);
	return true;
}

bool DposEvaluator::RegisteName(const TxMsg& _msg)
{
	bool ret = false;
	RegisteNameMsg& msg = (RegisteNameMsg&)_msg;
	auto& db = *DposDB::GetInstance();

	if(CheckStringFormat(msg.name, 2, 16, true)) {
		ret = db.SetAddressName(msg.nBlockHeight, msg.fromAddress, msg.name);
	}

	return ret;
}
