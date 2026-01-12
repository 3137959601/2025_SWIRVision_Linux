#ifndef MYCOMBOX_H
#define MYCOMBOX_H

#include <QComboBox>
#include <QListWidget>
#include <QMouseEvent>
#include <QStringList>

class myCombox : public QComboBox
{
    Q_OBJECT

public:
    explicit myCombox(QWidget *parent);
    ~myCombox();

    void addItem(QString str);
    void addItems(QStringList &str);
    void set_erasable(int idx, bool flag);

signals:
    void delete_item_name(const QString arg1);

private slots:
    void delete_item(const QString &arg1);
    void highlight_item(const QString &arg1);

protected:
    bool event(QEvent *event);

private:
    QListWidget *listwidget = nullptr;
    int view_width = 0;
};

#endif // MYCOMBOX_H
