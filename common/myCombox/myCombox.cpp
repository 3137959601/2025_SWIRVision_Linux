#include "myCombox.h"
#include "comboxItem.h"
#include <QLabel>
#include <QListWidgetItem>
//#include <QStringListIterator>
#include <QStringList>
myCombox::myCombox(QWidget *parent)
{
    listwidget = new QListWidget(parent);

    setModel(listwidget->model());
    setView(listwidget);

    //setEditable(true);
    setFont(QFont("Courier", 9));
    setMinimumWidth(50);
}

myCombox::~myCombox()
{

}

void myCombox::addItem(QString str)
{
    int str_width;

    comboxItem *widget = new comboxItem(str);
    QListWidgetItem *item = new QListWidgetItem(listwidget);
    item->setText(str);
    listwidget->setItemWidget(item, widget);
    connect(widget, SIGNAL(del_clicked(QString)), this, SLOT(delete_item(QString)));
    connect(widget, SIGNAL(mouse_enter(QString)), this, SLOT(highlight_item(QString)));

    //str_width = QFontMetrics(this->font()).width(str) + 36;
    str_width = QFontMetrics(this->font()).horizontalAdvance(str) + 36;
    if (str_width > view_width)
        view_width = str_width;

    if (view_width > this->width())
        this->view()->setFixedWidth(view_width);
}

void myCombox::addItems(QStringList &str)
{
    QStringListIterator it(str);

    while(it.hasNext()) {
        addItem(it.next());
    }
}

void myCombox::delete_item(const QString &arg1)
{
    int i, count;
    QListWidgetItem *item_del;

    count = listwidget->count();

    for (i = 0; i < count; i++) {
        item_del = listwidget->item(i);
        if (item_del->text() == arg1) {
            comboxItem *frame = (comboxItem *)(listwidget->itemWidget(item_del));
            disconnect(frame, SIGNAL(del_clicked(QString)), this, SLOT(delete_item(QString)));
            listwidget->takeItem(i);
            delete item_del;
            break;
        }
    }

    emit delete_item_name(arg1);
}

bool myCombox::event(QEvent *e)
{
    if(e->type() == QMouseEvent::MouseButtonPress) {
        int i, count = this->count();
        int width_temp;

        if (count == 0) {
            view_width = 0;
        } else {

            view_width = QFontMetrics(this->font()).horizontalAdvance(this->itemText(0)) + 36;

            for (i = 1; i < count; i++) {
                width_temp = QFontMetrics(this->font()).horizontalAdvance(this->itemText(i)) + 36;
                if (width_temp > view_width) {
                    view_width = width_temp;
                }
            }

            if (view_width > this->width()) {
                this->view()->setFixedWidth(view_width);
            } else {
                this->view()->setFixedWidth(this->width() - 2);
            }
        }
    }

    QComboBox::event(e);
    return true;
}

void myCombox::set_erasable(int idx, bool flag)
{
    comboxItem *widget;

    widget = (comboxItem *)(listwidget->itemWidget(listwidget->item(idx)));

    if (flag) {
        widget->show_del_btn();
    } else {
        widget->hide_del_btn();
    }
}

void myCombox::highlight_item(const QString &arg1)
{
    emit highlighted(arg1.toInt());
}
