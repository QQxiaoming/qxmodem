#ifndef QXMODEM_H
#define QXMODEM_H

#include <QString>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QMutex>
#include <QThread>
#include <QDebug>

class QXYmodem: public QThread {
    Q_OBJECT

public:
    explicit QXYmodem(int type, unsigned short sendPktSize = 128, int timeout = 1000, int retry_limit = 16, bool no_timeout = true,QObject *parent = nullptr):
        QThread(parent),m_type(type),m_sendPktSize(sendPktSize),m_timeout(timeout),m_retry_limit(retry_limit),m_no_timeout(no_timeout) {};
    ~QXYmodem(){};

    enum {
        SEND,
        RECV
    };
    enum {
        XMODEM,
        YMODEM
    };

    /* xmodem control characters */
    enum {
        SOH	  = 0x01,
        STX	  = 0x02,
        EOT	  = 0x04,
        ACK	  = 0x06,
        NAK	  = 0x15,
        CAN	  = 0x18,
        CTRLZ = 0x1A,
    };

    /* error return codes */
    enum {
        XMODEM_END = 1,
        XMODEM_OK = 0,
        XMODEM_ERROR_REMOTECANCEL = -1,
        XMODEM_ERROR_OUTOFSYNC	  = -2,
        XMODEM_ERROR_RETRYEXCEED  = -3,
    };

    void startSend(void) {
        dir=SEND;
        start();
    }

    void startRecv(void) {
        dir=RECV;
        start();
    }

protected:
    void run() {
        _start();
        if(m_type == XMODEM) {
            if(dir == SEND) {
                xmodemTransmit(m_sendPktSize);
            } else {
                xmodemReceive();
            }
        } else {
            if(dir == SEND) {
                int ret = 0;
                do {
                    ret = ymodemTransmit(m_sendPktSize);
                    transferOnce();
                } while(ret == 0);
            } else {
                int ret = 0;
                do {
                    ret = ymodemReceive();
                    transferOnce();
                } while(ret == 0);
            }
        }
        _end();
    }

private:
    virtual void _start(void) = 0;
    virtual void _end(void) = 0;

    virtual int writefile(const char* buffer, int size) = 0;
    virtual int readfile(char* buffer, int size) = 0;
    virtual int flushfile(void) = 0;
    virtual int writefileInfo(const char* buffer, int size) = 0;
    virtual int readfileInfo(char* buffer, int size) = 0;
    virtual int transferOnce(void) = 0;

    virtual int sendStream(const char* buffer, int size) = 0;
    virtual int receiveStream(const char* buffer, int size) = 0;

    virtual void timerPause(int t) {
        Q_UNUSED(t);
    }

    void xmodemOut(unsigned char c) {
        sendStream((const char*)&c,1);
    }

    int xmodemIn(unsigned char *c) {
        return receiveStream((const char*)c,1);
    }    
    
    uint16_t crc_xmodem_update(uint16_t crc, uint8_t data);
    long xmodemReceive(void);
    long ymodemReceive(void);
    long xmodemTransmit(unsigned short pktsize = 128);
    long ymodemTransmit(unsigned short pktsize = 1024);
    int xmodemCrcCheck(int crcflag, const unsigned char *buffer, int size);
    int xmodemInTime(unsigned char *c, unsigned short timeout);
    void xmodemInFlush(void);

private:
    int dir=SEND;
    int m_type=XMODEM;

    /* xmodem parameters */
    unsigned short m_sendPktSize = 128;
    int m_timeout = 1000;
    int m_retry_limit = 16;
    bool m_no_timeout = true;
};

class QXmodemFile: public QXYmodem {
    Q_OBJECT

public:
    QXmodemFile(QString filename,unsigned short sendPktSize = 128, int timeout = 1000, int retry_limit = 16, bool no_timeout = true, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::XMODEM,sendPktSize,timeout,retry_limit,no_timeout,parent)
    {
        m_file = new QFile(filename);
    }
    QXmodemFile(const char *filename,unsigned short sendPktSize = 128, int timeout = 1000, int retry_limit = 16, bool no_timeout = true,QObject *parent = nullptr) :
        QXYmodem(QXYmodem::XMODEM,sendPktSize,timeout,retry_limit,no_timeout,parent)
    {
        m_file = new QFile(QString(filename));
    }
    QXmodemFile(QString filename, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::XMODEM,128,1000,16,true,parent)
    {
        m_file = new QFile(filename);
    }
    QXmodemFile(const char *filename, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::XMODEM,128,1000,16,true,parent)
    {
        m_file = new QFile(QString(filename));
    }
    ~QXmodemFile(){
        delete m_file;
    }
    
signals:
    void send(QByteArray ba);

public slots:
    void receive(QByteArray ba) {
        m_mutex.lock();
        cache.append(ba);
        m_mutex.unlock();
    }

private:
    void _start(void) {
        m_file->open(QIODevice::ReadWrite);
    }

    void _end(void) {
        m_file->close();
    }

    int writefile(const char* buffer, int size) {
        return m_file->write(buffer,size);
    }
    int readfile(char* buffer, int size) {
        int r = m_file->read(buffer,size);
        return r;
    }
    int flushfile(void)
    {
        if(m_file) {
            if(m_file->isOpen())
                return m_file->flush();
        }
        return 0;
    }
    int writefileInfo(const char* buffer, int size) {
        Q_UNUSED(buffer);
        Q_UNUSED(size);
        return 0;
    }
    int readfileInfo(char* buffer, int size) {
        Q_UNUSED(buffer);
        Q_UNUSED(size);
        return 0;
    }
    int transferOnce(void) {
        return 0;
    }
    int sendStream(const char* buffer, int size) {
        emit send(QByteArray(buffer,size));
        return size;
    }
    int receiveStream(const char* buffer, int size) {
        m_mutex.lock();
        int ret = qMin(size,cache.size());
        memcpy((void*)buffer,cache.data(),ret);
        cache.remove(0,ret);
        m_mutex.unlock();
        return ret;
    }
    void timerPause(int t) {
        QThread::msleep(t);
    }

private:
    QFile *m_file = nullptr;
    QMutex m_mutex;
    QByteArray cache;
};

class QYmodemFile: public QXYmodem {
    Q_OBJECT

public:
    QYmodemFile(QStringList filePathList,unsigned short sendPktSize = 1024, int timeout = 1000, int retry_limit = 16, bool no_timeout = true, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::YMODEM,sendPktSize,timeout,retry_limit,no_timeout,parent)
    {
        m_filePathList = filePathList;
    }
    QYmodemFile(QStringList filePathList, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::YMODEM,1024,1000,16,true,parent)
    {
        m_filePathList = filePathList;
    }
    QYmodemFile(QString filePathDir,unsigned short sendPktSize = 1024, int timeout = 1000, int retry_limit = 16, bool no_timeout = true, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::YMODEM,sendPktSize,timeout,retry_limit,no_timeout,parent)
    {
        m_filePathDir = filePathDir;
    }
    QYmodemFile(QString filePathDir, QObject *parent = nullptr) :
        QXYmodem(QXYmodem::YMODEM,1024,1000,16,true,parent)
    {
        m_filePathDir = filePathDir;
    }
    ~QYmodemFile(){
    }
    
signals:
    void send(QByteArray ba);

public slots:
    void receive(QByteArray ba) {
        m_mutex.lock();
        cache.append(ba);
        m_mutex.unlock();
    }

private:
    void _start(void) {
    }

    void _end(void) {
    }

    int writefile(const char* buffer, int size) {
        if(m_currentSize+size > m_fileSize) {
            size = m_fileSize - m_currentSize;
        }
        int w = m_file->write(buffer,size);
        m_currentSize += w;
        return w;
    }
    int readfile(char* buffer, int size) {
        int r = m_file->read(buffer,size);
        m_currentSize += r;
        return r;
    }
    int flushfile(void)
    {
        if(m_file) {
            if(m_file->isOpen())
                return m_file->flush();
        }
        return 0;
    }
    int readfileInfo(char* buffer, int size) {
        if(m_fileIndex < m_filePathList.size()) {
            QString filename = m_filePathList.at(m_fileIndex);
            m_file = new QFile(filename);
            QFileInfo fileInfo(filename);
            strcpy((char *)buffer, fileInfo.fileName().toLocal8Bit().data());
            strcpy((char *)buffer + fileInfo.fileName().toLocal8Bit().size() + 1, QByteArray::number(fileInfo.size()).data());
            m_file->open(QIODevice::ReadWrite);
            m_fileSize  = fileInfo.size();
            m_currentSize = 0;
            m_fileIndex++;
            return size;
        } else {
            return 0;
        }
    }
    int writefileInfo(const char* buffer, int s) {
        if(m_filePathDir.size() > 0) {
            int  i =  0;
            char name[128] = {0};
            char size[128] = {0};

            for(int j = 0; buffer[i] != 0; i++, j++) {
                name[j] = buffer[i];
            }
            i++;
            for(int j = 0; buffer[i] != 0; i++, j++) {
                size[j] = buffer[i];
            }

            QString fileName = QString::fromLocal8Bit(name);
            QString file_desc(size);
            QString sizeStr = file_desc.left(file_desc.indexOf(' '));
            m_fileSize  = sizeStr.toULongLong();
            m_currentSize = 0;

            m_file = new QFile(m_filePathDir + QDir::separator() + fileName);
            m_file->open(QIODevice::ReadWrite);
            return s;
        } else {
            return 0;
        }
    }
    int transferOnce(void) {
        if(m_file){
            if(m_file->isOpen()) {
                m_file->close();
                delete m_file;
                m_file = nullptr;
            }
        }
    }
    int sendStream(const char* buffer, int size) {
        emit send(QByteArray(buffer,size));
        return size;
    }
    int receiveStream(const char* buffer, int size) {
        m_mutex.lock();
        int ret = qMin(size,cache.size());
        memcpy((void*)buffer,cache.data(),ret);
        cache.remove(0,ret);
        m_mutex.unlock();
        return ret;
    }
    void timerPause(int t) {
        QThread::msleep(t);
    }

private:
    QFile *m_file = nullptr;
    QStringList m_filePathList;
    QString m_filePathDir;
    int m_fileIndex = 0;
    qint64 m_fileSize = 0;
    qint64 m_currentSize = 0;
    QMutex m_mutex;
    QByteArray cache;
};

#endif /* QXMODEM_H */ 
