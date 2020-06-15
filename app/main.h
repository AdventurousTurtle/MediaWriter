#ifndef MAIN_H_
#define MAIN_H_

#include <QProcess>
#include <unistd.h>

class PipeProcess : public QProcess {
    Q_OBJECT
public:
    PipeProcess(QObject *parent = nullptr, int fd)
        : QProcess(parent)
        , m_fd(fd)
    {

    }

    void setupChildProcess() override {
        ::close(fileno(stdin));
        ::dup2(m_fd, fileno(stdin));
    }
private:
    int m_fd;
};

#endif
