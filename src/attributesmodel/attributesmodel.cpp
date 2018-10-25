#include "attributesmodel.h"

#include "../theapp.h"
#include "../servertreemodel/shvnodeitem.h"

#include <shv/chainpack/cponreader.h>
//#include <shv/chainpack/cponwriter.h>
#include <shv/chainpack/rpcvalue.h>
#include <shv/core/utils.h>
#include <shv/core/assert.h>
#include <shv/coreqt/log.h>
#include <shv/iotqt/rpc/rpc.h>

#include <QSettings>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QIcon>

namespace cp = shv::chainpack;

AttributesModel::AttributesModel(QObject *parent)
	: Super(parent)
{
}

AttributesModel::~AttributesModel()
{
}

int AttributesModel::rowCount(const QModelIndex &parent) const
{
	Q_UNUSED(parent)
	if(m_shvTreeNodeItem.isNull())
		return 0;
	return m_shvTreeNodeItem->methods().count();
}

Qt::ItemFlags AttributesModel::flags(const QModelIndex &ix) const
{
	Qt::ItemFlags ret = Super::flags(ix);
	if(ix.column() == ColParams)
		ret |= Qt::ItemIsEditable;
	if(ix.column() == ColBtRun)
		ret &= ~Qt::ItemIsSelectable;
	return ret;
}

QVariant AttributesModel::data(const QModelIndex &ix, int role) const
{
	if(m_shvTreeNodeItem.isNull())
		return QVariant();
	const QVector<ShvMetaMethod> &mms = m_shvTreeNodeItem->methods();
	if(ix.row() < 0 || ix.row() >= mms.count())
		return QVariant();

	switch (role) {
	case Qt::DisplayRole: {
		switch (ix.column()) {
		case ColMethodName:
		case ColSignature:
		case ColParams:
			return m_rows.value(ix.row()).value(ix.column());
		case ColResult:
			if(m_rows.value(ix.row()).value(ColIsError).toBool()) {
				return m_rows.value(ix.row()).value(ix.column());
			}
			else {
				cp::RpcValue rv = qvariant_cast<cp::RpcValue>(m_rows.value(ix.row()).value(ColRawResult));

				static constexpr int MAX_TT_SIZE = 1024;
				std::string tts = rv.toPrettyString("  ");
				if(tts.size() > MAX_TT_SIZE)
					tts = tts.substr(0, MAX_TT_SIZE) + " < ... " + std::to_string(tts.size() - MAX_TT_SIZE) + " more bytes >";

				return QString::fromStdString(tts);
			}
		case ColFlags:
			return m_rows.value(ix.row()).value(ix.column()).toBool()? "Y": QVariant();
		default:
			break;
		}
		break;
	}
	case Qt::EditRole: {
		switch (ix.column()) {
		case ColParams:
			return m_rows.value(ix.row()).value(ix.column());
		default:
			break;
		}
		break;
	}
	case Qt::DecorationRole: {
		if(ix.column() == ColBtRun) {
			bool is_notify = m_rows.value(ix.row()).value(ColFlags).toBool();
			if(!is_notify) {
				static QIcon ico_run = QIcon(QStringLiteral(":/shvspy/images/run"));
				static QIcon ico_reload = QIcon(QStringLiteral(":/shvspy/images/reload"));
				auto v = m_rows.value(ix.row()).value(ColBtRun);
				return (v.toUInt() > 0)? ico_reload: ico_run;
			}
		}
		break;
	}
	case Qt::ToolTipRole: {
		if(ix.column() == ColBtRun) {
			return tr("Call remote method");
		}
		else if(ix.column() == ColResult) {
			return data(ix, Qt::DisplayRole);
		}
		else if(ix.column() == ColFlags) {
			bool is_notify = m_rows.value(ix.row()).value(ColFlags).toBool();
			return is_notify? tr("Method is notify signal"): QVariant();
		}
		else {
			return data(ix, Qt::DisplayRole);
		}
	}
	case RawResultRole: {
		return m_rows.value(ix.row()).value(ColRawResult);
	}
	default:
		break;
	}
	return QVariant();
}

bool AttributesModel::setData(const QModelIndex &ix, const QVariant &val, int role)
{
	shvLogFuncFrame() << val.toString() << val.typeName() << "role:" << role;
	if(role == Qt::EditRole) {
		if(ix.column() == ColParams) {
			if(!m_shvTreeNodeItem.isNull()) {
				cp::RpcValue params;
				std::string cpon = val.toString().toStdString();
				if(!cpon.empty()) {
					std::string err;
					params = cp::RpcValue::fromCpon(cpon, &err);
					if(!err.empty())
						shvWarning() << "Invalid method param:" << cpon;
				}
				m_shvTreeNodeItem->setMethodParams(ix.row(), params);
				loadRow(ix.row());
				return true;
			}
		}
	}
	return false;
}

QVariant AttributesModel::headerData(int section, Qt::Orientation o, int role) const
{
	QVariant ret;
	if(o == Qt::Horizontal) {
		if(role == Qt::DisplayRole) {
			if(section == ColMethodName)
				ret = tr("Method");
			else if(section == ColSignature)
				ret = tr("Signature");
			else if(section == ColFlags)
				ret = tr("Flags");
			else if(section == ColAccessLevel)
				ret = tr("AL");
			else if(section == ColParams)
				ret = tr("Params");
			else if(section == ColResult)
				ret = tr("Result");
		}
		else if(role == Qt::ToolTipRole) {
			if(section == ColAccessLevel)
				ret = tr("Acess Level");
		}
	}
	return ret;
}

void AttributesModel::load(ShvNodeItem *nd)
{
	m_rows.clear();
	if(!m_shvTreeNodeItem.isNull())
		m_shvTreeNodeItem->disconnect(this);
	m_shvTreeNodeItem = nd;
	if(nd) {
		connect(nd, &ShvNodeItem::methodsLoaded, this, &AttributesModel::onMethodsLoaded, Qt::UniqueConnection);
		connect(nd, &ShvNodeItem::rpcMethodCallFinished, this, &AttributesModel::onRpcMethodCallFinished, Qt::UniqueConnection);
		nd->checkMethodsLoaded();
	}
	loadRows();
}

void AttributesModel::callMethod(int method_ix)
{
	if(m_shvTreeNodeItem.isNull())
		return;
	unsigned rqid = m_shvTreeNodeItem->callMethod(method_ix);
	m_rows[method_ix][ColBtRun] = rqid;
	emitRowChanged(method_ix);
}

QString AttributesModel::path() const
{
	if (m_shvTreeNodeItem.isNull()) {
		return QString();
	}
	return QString::fromStdString(m_shvTreeNodeItem->shvPath());
}

QString AttributesModel::method(int row) const
{
	if (m_shvTreeNodeItem.isNull()) {
		return QString();
	}
	return QString::fromStdString(m_shvTreeNodeItem->methods()[row].method);
}

void AttributesModel::onMethodsLoaded()
{
	loadRows();
	callGet();
}

void AttributesModel::onRpcMethodCallFinished(int method_ix)
{
	loadRow(method_ix);
	emitRowChanged(method_ix);
}

void AttributesModel::emitRowChanged(int row_ix)
{
	QModelIndex ix1 = index(row_ix, 0);
	QModelIndex ix2 = index(row_ix, ColCnt - 1);
	emit dataChanged(ix1, ix2);
}

void AttributesModel::callGet()
{
	for (int i = 0; i < m_rows.count(); ++i) {
		QString mn = m_rows[i].value(ColMethodName).toString();
		if(mn == QLatin1String(cp::Rpc::METH_GET)) {
			callMethod(i);
		}
	}
}

void AttributesModel::loadRow(int method_ix)
{
	if(method_ix < 0 || method_ix >= m_rows.count() || m_shvTreeNodeItem.isNull())
		return;
	const QVector<ShvMetaMethod> &mm = m_shvTreeNodeItem->methods();
	const ShvMetaMethod & mtd = mm[method_ix];
	RowVals &rv = m_rows[method_ix];
	rv[ColMethodName] = QString::fromStdString(mtd.method);
	rv[ColSignature] = QString::fromStdString(mtd.signatureStr());
	rv[ColFlags] = QString::fromStdString(mtd.flagsStr());
	rv[ColAccessLevel] = QString::fromStdString(mtd.accessLevelStr());
	if(mtd.params.isValid()) {
		rv[ColParams] = QString::fromStdString(mtd.params.toCpon());
	}
	if(mtd.response.isError()) {
		//rv[ColRawResult] = QVariant::fromValue(shv::chainpack::RpcValue(mtd.response.error()));
		rv[ColRawResult] = QVariant();
		rv[ColResult] = QString::fromStdString(mtd.response.error().toString());
		rv[ColIsError] = true;
	}
	else if(mtd.response.result().isValid()) {
		rv[ColRawResult] = QVariant::fromValue(mtd.response.result());
		shv::chainpack::RpcValue result = mtd.response.result();
		if(result.isString())
			rv[ColResult] = QString::fromStdString(result.toString());
		else
			rv[ColResult] = QString::fromStdString(mtd.response.result().toCpon());
		rv[ColIsError] = false;
	}
	rv[ColBtRun] = mtd.rpcRequestId;
}

void AttributesModel::loadRows()
{
	m_rows.clear();
	if(!m_shvTreeNodeItem.isNull()) {
		const QVector<ShvMetaMethod> &mm = m_shvTreeNodeItem->methods();
		for (int i = 0; i < mm.count(); ++i) {
			RowVals rv;
			rv.resize(ColCnt);
			m_rows.insert(m_rows.count(), rv);
			loadRow(m_rows.count() - 1);
		}
	}
	emit layoutChanged();
	emit reloaded();
}

/*
void AttributesModel::onRpcMessageReceived(const shv::chainpack::RpcMessage &msg)
{
	if(msg.isResponse()) {
		cp::RpcResponse resp(msg);
		if(resp.requestId() == m_rpcRqId) {
			for(const cp::RpcValue &val : resp.result().toList()) {
				appendRow(QList<QStandardItem*>{
							  new QStandardItem(QString::fromStdString(val.toString())),
							  new QStandardItem("<not called>"),
						  });
			}
		}
	}
}
*/
