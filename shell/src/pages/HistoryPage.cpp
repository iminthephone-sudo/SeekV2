#include "pages/HistoryPage.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/AppContext.h"

HistoryPage::HistoryPage(AppContext* ctx, QWidget* parent) : Page(parent), m_ctx(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(28, 22, 28, 22);
    col->setSpacing(14);
    auto* tools = new QWidget;
    auto* tl = new QHBoxLayout(tools);
    tl->setContentsMargins(0, 0, 0, 0);
    m_kind = new QComboBox;
    m_kind->addItem(tr("All activity"), QString());
    m_kind->addItem(tr("Profiles"), "profile");
    m_kind->addItem(tr("Job postings"), "job");
    m_kind->addItem(tr("Optimizer"), "optimize");
    m_kind->addItem(tr("Cover letters"), "letter");
    m_kind->addItem(tr("Resume exports"), "resume");
    auto* refresh = ui::flatButton("arrow-repeat", tr("Refresh"));
    tl->addWidget(m_kind);
    tl->addWidget(refresh);
    col->addWidget(ui::pageHeader(tr("Activity"), tr("A durable log of everything done in SEEK — useful for case notes "
                                                     "and program reporting. Stored locally."), tools));
    auto* card = new Card;
    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({tr("When"), tr("Type"), tr("What happened")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(false);
    card->body()->addWidget(m_table);
    col->addWidget(card, 1);
    connect(m_kind, &QComboBox::currentIndexChanged, this, &HistoryPage::refresh);
    connect(refresh, &QPushButton::clicked, this, &HistoryPage::refresh);
}

void HistoryPage::activate(const QVariantMap&) { refresh(); }

void HistoryPage::refresh() {
    QJsonObject params{{"limit", 500}};
    if (!m_kind->currentData().toString().isEmpty()) params.insert("kind", m_kind->currentData().toString());
    m_ctx->bridge()->call("history.list", params, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Loading activity"), e);
        const QJsonArray rows = r.toArray();
        m_table->setRowCount(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            const QJsonObject h = rows.at(i).toObject();
            const QDateTime when = QDateTime::fromString(h.value("at").toString(), Qt::ISODate).toLocalTime();
            m_table->setItem(i, 0, new QTableWidgetItem(QLocale().toString(when, QLocale::ShortFormat)));
            m_table->setItem(i, 1, new QTableWidgetItem(h.value("kind").toString()));
            m_table->setItem(i, 2, new QTableWidgetItem(h.value("summary").toString()));
        }
    }, this);
}
