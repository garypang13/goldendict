/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#ifndef __LOADDICTIONARIES_HH_INCLUDED__
#define __LOADDICTIONARIES_HH_INCLUDED__

#include "config.hh"
#include "dictionary.hh"
#include <QThread>
class QNetworkAccessManager;

/// Use loadDictionaries() function below -- this is a helper thread class
class LoadDictionaries: public QThread, public Dictionary::Initializing
{
  Q_OBJECT
private:
  std::vector< sptr< Dictionary::Class > > &dictionaries;
  QElapsedTimer const & timer;
  Config::Class const & cfg_;
  std::string exceptionText;
#ifdef DICTS_LOADING_CONCURRENT
  QSemaphore sWait;
  QMutex sMutex;
  QAtomicInt ref;
#endif

protected:
  virtual void run();

signals:
  void showMessage(const QString &message, int align=Qt::AlignCenter, const QColor &color = Qt::darkMagenta);

public:
  ~LoadDictionaries(){}
  virtual void indexingDictionary( std::string const & dictionaryName ) throw();

private:
  LoadDictionaries( Config::Class const & cfg, QElapsedTimer const & timer, std::vector< sptr< Dictionary::Class > > &dicts );
//  std::vector< sptr< Dictionary::Class > > const & getDictionaries() const
//  { return dictionaries; }

  /// Empty string means to exception occurred
  std::string const & getExceptionText() const
  { return exceptionText; }
  void handlePath( Config::Path const & );

public:
#ifdef DICTS_LOADING_CONCURRENT
  void addDictionaries(const std::vector< sptr< Dictionary::Class > > &dics, const std::string &e1 = std::string()) {
      sMutex.lock();
      if(!e1.empty())
        exceptionText.append(e1);
      dictionaries.insert( dictionaries.end(), dics.begin(), dics.end() );
      sMutex.unlock();
      --ref;
      sWait.release();
  }
#endif
#ifdef DICTS_LOADING_CONCURRENT
  void handleFiles(std::vector< sptr< Dictionary::Class > > &dictionaries,
                   const std::vector< std::string > &allFiles);
#else
  void handleFiles(const std::vector< std::string > &allFiles);
#endif

  /// Loads all dictionaries mentioned in the configuration passed, into the
  /// supplied array. When necessary, a window would pop up describing the process.
  /// If showInitially is passed as true, the window will always popup.
  /// If doDeferredInit is true (default), doDeferredInit() is done on all
  /// dictionaries at the end.
  static void loadDictionaries( QWidget * parent, bool canHideParent,
                         Config::Class const & cfg,
                         std::vector< sptr< Dictionary::Class > > &,
                         QNetworkAccessManager & dictNetMgr,
                         bool doDeferredInit = true );

  /// Runs deferredInit() on all the given dictionaries. Useful when
  /// loadDictionaries() was previously called with doDeferredInit = false.
  static void doDeferredInit( std::vector< sptr< Dictionary::Class > > & );

};

#ifdef DICTS_LOADING_CONCURRENT
class LoadDictionariesRunnable : public QRunnable
{
public:
    LoadDictionariesRunnable(LoadDictionaries & _ld, std::vector< std::string > &_allFiles)
        : ld(_ld), allFiles(_allFiles)
    {}
    ~LoadDictionariesRunnable()
    {}
    void run();

private:
    LoadDictionaries &ld;
    std::vector< std::string > allFiles;
};
#endif

#endif

