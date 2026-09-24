#pragma once

#include "ui/Widgets.h"

class AppContext;
class QComboBox;
class QTabWidget;
class QTextBrowser;

class ResumePage : public Page {
    Q_OBJECT
public:
    explicit ResumePage(AppContext* ctx, QWidget* parent = nullptr);
    void activate(const QVariantMap& args) override;

    // Shared with the cover letter page: print HTML/plain text to a PDF file.
    static bool writePdf(const QString& path, const QString& html, bool plainText = false);

private:
    void loadProfile();
    void render();
    void analyze();
    void review();
    void exportAs(const QString& format);
    QString profileId() const;
    QString suggestedFileName(const QString& ext) const;

    AppContext* m_ctx;
    QComboBox* m_profile;
    QComboBox* m_template;
    QTextBrowser* m_preview;
    QTabWidget* m_side;
    ScoreRing* m_score;
    QVBoxLayout* m_checks;
    QVBoxLayout* m_bullets;
    QLabel* m_fcSummary;
    QVBoxLayout* m_findings;
    QVBoxLayout* m_guidance;
    QString m_html;
    QString m_fullName;
};
