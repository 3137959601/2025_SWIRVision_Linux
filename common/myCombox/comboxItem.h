#ifndef COMBOXITEM_H
#define COMBOXITEM_H

#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QString>
#include <QMouseEvent>

class comboxItem : public QWidget
{
    Q_OBJECT

public:
    explicit comboxItem(QString text, bool del = true);
    ~comboxItem();
    void show_del_btn();
    void hide_del_btn();

signals:
    void del_clicked(const QString &name);
    void mouse_enter(const QString &name);

private slots:
    void btn_clicked();

protected:
    bool event(QEvent *event);

private:
    QHBoxLayout *layout = nullptr;
    QPushButton *btn = nullptr;
    QString     name;
};

#endif // COMBOXITEM_H
