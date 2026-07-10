#ifndef TELEMETRYDEBUGDIALOG_H
#define TELEMETRYDEBUGDIALOG_H

#include "uartprotocol.h"

#include <QDialog>
#include <QFile>
#include <QHash>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class TelemetryDebugDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TelemetryDebugDialog(QWidget *parent = nullptr);
    void handleFrame(const TelemetryFrame &frame);
    void addChecksumErrors(int count);

signals:
    void commandRequested(const QByteArray &command);

private:
    QWidget *buildStatusPanel();
    QWidget *buildCommandPanel();
    void emitCommand(quint8 code, quint8 control, quint16 value = 0);
    void setValue(const QString &key, const QString &value);
    void chooseCsvFile();
    void toggleCsv();
    void writeCsvRow(const TelemetryFrame &frame);

    QHash<QString, QLabel*> m_labels;
    QLabel *m_errorLabel = nullptr;
    QLabel *m_csvLabel = nullptr;
    QPushButton *m_csvButton = nullptr;
    QDoubleSpinBox *m_intTime = nullptr;
    QDoubleSpinBox *m_comVoltage = nullptr;
    QDoubleSpinBox *m_tecTemp = nullptr;
    QSpinBox *m_crossX = nullptr;
    QSpinBox *m_crossY = nullptr;
    QHash<int, QPushButton*> m_regionButtons;
    QLineEdit *m_rawCommand = nullptr;
    QFile m_csvFile;
    QString m_csvPath;
    int m_checksumErrors = 0;
};

#endif // TELEMETRYDEBUGDIALOG_H
