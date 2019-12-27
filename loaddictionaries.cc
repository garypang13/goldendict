/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "loaddictionaries.hh"
#include "initializing.hh"
#include "bgl.hh"
#include "stardict.hh"
#include "lsa.hh"
#include "dsl.hh"
#include "mediawiki.hh"
#include "sounddir.hh"
#include "hunspell.hh"
#include "dictdfiles.hh"
#include "romaji.hh"
#include "russiantranslit.hh"
#include "german.hh"
#include "greektranslit.hh"
#include "belarusiantranslit.hh"
#include "website.hh"
#include "forvo.hh"
#include "programs.hh"
#include "voiceengines.hh"
#include "gddebug.hh"
#include "fsencoding.hh"
#include "xdxf.hh"
#include "sdict.hh"
#include "aard.hh"
#include "zipsounds.hh"
#include "mdx.hh"
#include "zim.hh"
#include "dictserver.hh"
#include "slob.hh"
#include "gls.hh"

#ifndef NO_EPWING_SUPPORT
#include "epwing.hh"
#endif

#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
#include "chinese.hh"
#endif
#include <QSplashScreen>
#include <QMessageBox>
#include <QDir>

#include <set>

using std::set;

using std::string;
using std::vector;

LoadDictionaries::LoadDictionaries(Config::Class const & cfg , std::vector<sptr<Dictionary::Class> > &dicts, QElapsedTimer &timer_ )
: dictionaries(dicts), timer(timer_), cfg_(cfg)
#ifdef DICTS_LOADING_CONCURRENT
  ,sWait(0), ref_(0)
#endif
{
  // Populate name filters

  nameFilters << "*.bgl" << "*.ifo" << "*.lsa" << "*.dat"
              << "*.dsl" << "*.dsl.dz"  << "*.index" << "*.xdxf"
              << "*.xdxf.dz" << "*.dct" << "*.aar" << "*.zips"
              << "*.mdx" << "*.gls" << "*.gls.dz"
#ifdef MAKE_SLOB_SUPPORT
              << "*.slob"
#endif
#ifdef MAKE_ZIM_SUPPORT
              << "*.zim" << "*.zimaa"
#endif
#ifndef NO_EPWING_SUPPORT
              << "*catalogs"
#endif
;
}

void LoadDictionaries::run()
{
  emit showMessage(tr("Start Handling Dictionaries\n..."));
  try
  {
    for( Config::Paths::const_iterator i = cfg_.paths.begin(); i != cfg_.paths.end(); ++i )
      handlePath( *i );
#ifdef DICTS_LOADING_CONCURRENT
    while(ref_)
    {
      int left = ref_;
      while(!sWait.tryAcquire(1, 1000))
        emit showMessage(tr("Handling User's Dictionary: [ %1 ] left\nTime elapsed: %2 s").
                         arg(left).arg(timer.elapsed() / 1000));
    };
#endif
    if(!exceptionText.empty() && dictionaries.empty())
    {
        emit showMessage(tr("Failed to Handle User's Dictionaries.\n%1").
                         arg(QString::fromUtf8(exceptionText.c_str())));
        return;
    }
#ifndef DICTS_LOADING_CONCURRENT
    else
    {
        emit showMessage(tr("[ %1 ] User's Dictionaries Handled.\nTime elapsed: %2 s").
                         arg(dictionaries.size()).arg(timer.elapsed() / 1000) );
    }
#endif

    // Make soundDirs
    {
      vector< sptr< Dictionary::Class > > soundDirDictionaries =
        SoundDir::makeDictionaries( cfg_.soundDirs, FsEncoding::encode( Config::getIndexDir() ), *this );

      dictionaries.insert( dictionaries.end(), soundDirDictionaries.begin(),
                           soundDirDictionaries.end() );
      if(!soundDirDictionaries.empty())
          emit showMessage(tr("[ %1 ] SoundDir Dictionaries Handled.\nTime elapsed: %2 s").
                           arg(soundDirDictionaries.size()).arg(timer.elapsed() / 1000) );
    }

    // Make hunspells
    {
      vector< sptr< Dictionary::Class > > hunspellDictionaries =
        HunspellMorpho::makeDictionaries( cfg_.hunspell );

      dictionaries.insert( dictionaries.end(), hunspellDictionaries.begin(),
                           hunspellDictionaries.end() );
      if(!hunspellDictionaries.empty())
          emit showMessage(tr("[ %1 ] HunspellMorpho Dictionaries Handled.\nTime elapsed: %2 s").
                           arg(hunspellDictionaries.size()).arg(timer.elapsed() / 1000) );
    }
  }
  catch( std::exception & e )
  {
    exceptionText.append(e.what());
  }
  emit showMessage(tr("Finished Handling Dictionaries [ %1 ].\nTime elapsed: %2 s").
                   arg(dictionaries.size()).
                   arg(timer.elapsed() / 1000) );
}

void LoadDictionaries::handlePath( Config::Path const & path )
{
  vector< string > allFiles;

  QDir dir( path.path );

  QFileInfoList entries = dir.entryInfoList( nameFilters, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot );

  for( QFileInfoList::const_iterator i = entries.constBegin();
       i != entries.constEnd(); ++i )
  {
    QString fullName = i->absoluteFilePath();

    if ( path.recursive && i->isDir() )
    {
      // Make sure the path doesn't look like with dsl resources
      if ( !fullName.endsWith( ".dsl.files", Qt::CaseInsensitive ) &&
           !fullName.endsWith( ".dsl.dz.files", Qt::CaseInsensitive ) )
        handlePath( Config::Path( fullName, true ) );
    }

    if ( !i->isDir() )
      allFiles.push_back( FsEncoding::encode( QDir::toNativeSeparators( fullName ) ) );
  }
  if(allFiles.empty())
     return;

  emit showMessage(tr("Handling User's Dictionary.\n%1").arg(path.path));
#ifdef DICTS_LOADING_CONCURRENT
  sMutex.lock();
  ++ref_;
  sMutex.unlock();
  QRunnable *r = new LoadDictionariesRunnable(*this, allFiles);
  QThreadPool::globalInstance()->start(r);
#else
  handleFiles(allFiles);
#endif
}

#ifdef DICTS_LOADING_CONCURRENT
void LoadDictionaries::handleFiles(std::vector< sptr< Dictionary::Class > > &dictionaries,
                                   const std::vector< std::string > &allFiles)
#else
void LoadDictionaries::handleFiles(const std::vector< std::string > &allFiles)
#endif
{
  {
    vector< sptr< Dictionary::Class > > bglDictionaries =
      Bgl::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this );

    dictionaries.insert( dictionaries.end(), bglDictionaries.begin(),
                         bglDictionaries.end() );
  }

  {
    vector< sptr< Dictionary::Class > > stardictDictionaries =
      Stardict::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this, cfg_.maxHeadwordsToExpand );

    dictionaries.insert( dictionaries.end(), stardictDictionaries.begin(),
                         stardictDictionaries.end() );
  }

  {
    vector< sptr< Dictionary::Class > > lsaDictionaries =
      Lsa::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this );

    dictionaries.insert( dictionaries.end(), lsaDictionaries.begin(),
                         lsaDictionaries.end() );
  }

  {
    vector< sptr< Dictionary::Class > > dslDictionaries =
      Dsl::makeDictionaries(
          allFiles, FsEncoding::encode( Config::getIndexDir() ), *this, cfg_.maxPictureWidth, cfg_.maxHeadwordSize );

    dictionaries.insert( dictionaries.end(), dslDictionaries.begin(),
                         dslDictionaries.end() );
  }

  {
    vector< sptr< Dictionary::Class > > dictdDictionaries =
      DictdFiles::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this );

    dictionaries.insert( dictionaries.end(), dictdDictionaries.begin(),
                         dictdDictionaries.end() );
  }
  {
    vector< sptr< Dictionary::Class > > xdxfDictionaries =
      Xdxf::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this );

    dictionaries.insert( dictionaries.end(), xdxfDictionaries.begin(),
                         xdxfDictionaries.end() );
  }
  {
    vector< sptr< Dictionary::Class > > sdictDictionaries =
      Sdict::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this );

    dictionaries.insert( dictionaries.end(), sdictDictionaries.begin(),
                         sdictDictionaries.end() );
  }
  {
    vector< sptr< Dictionary::Class > > aardDictionaries =
      Aard::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this, cfg_.maxHeadwordsToExpand );

    dictionaries.insert( dictionaries.end(), aardDictionaries.begin(),
                         aardDictionaries.end() );
  }
  {
    vector< sptr< Dictionary::Class > > zipSoundsDictionaries =
      ZipSounds::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this );

    dictionaries.insert( dictionaries.end(), zipSoundsDictionaries.begin(),
                         zipSoundsDictionaries.end() );
  }
  {
    vector< sptr< Dictionary::Class > > mdxDictionaries =
      Mdx::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this );

    dictionaries.insert( dictionaries.end(), mdxDictionaries.begin(),
                         mdxDictionaries.end() );
  }
  {
    vector< sptr< Dictionary::Class > > glsDictionaries =
      Gls::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this );

    dictionaries.insert( dictionaries.end(), glsDictionaries.begin(),
                         glsDictionaries.end() );
  }
#ifdef MAKE_ZIM_SUPPORT
  {
    vector< sptr< Dictionary::Class > > zimDictionaries =
      Zim::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this, cfg_.maxHeadwordsToExpand );

    dictionaries.insert( dictionaries.end(), zimDictionaries.begin(),
                         zimDictionaries.end() );
  }
#endif
#ifdef MAKE_SLOB_SUPPORT
  {
    vector< sptr< Dictionary::Class > > slobDictionaries =
      Slob::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this, cfg_.maxHeadwordsToExpand );

    dictionaries.insert( dictionaries.end(), slobDictionaries.begin(),
                         slobDictionaries.end() );
  }
#endif
#ifndef NO_EPWING_SUPPORT
  {
    vector< sptr< Dictionary::Class > > epwingDictionaries =
      Epwing::makeDictionaries( allFiles, FsEncoding::encode( Config::getIndexDir() ), *this );

    dictionaries.insert( dictionaries.end(), epwingDictionaries.begin(),
                         epwingDictionaries.end() );
  }
#endif
}

void LoadDictionaries::indexingDictionary( string const & dictionaryName ) throw()
{
  emit showMessage( tr("Indexing Dictionary.\n%1").arg(QString::fromUtf8( dictionaryName.c_str() )) );
}

#ifdef DICTS_LOADING_CONCURRENT
void LoadDictionariesRunnable::run()
{
    std::string exceptionText;
    std::vector< sptr< Dictionary::Class > > dictionaries;
    try
    {
        ld.handleFiles(dictionaries, allFiles);
    }
    catch( std::exception & e )
    {
      exceptionText = e.what();
    }
    ld.addDictionaries(dictionaries, exceptionText);
}
#endif

void LoadDictionaries::loadDictionaries( QWidget * parent, bool canHideParent,
                       Config::Class const & cfg,
                       std::vector< sptr< Dictionary::Class > > & dictionaries,
                       QNetworkAccessManager & dictNetMgr,
                       bool doDeferredInit_ )
{
  QElapsedTimer timer;
  timer.start();
  const int expiryTimeout = QThreadPool::globalInstance()->expiryTimeout();
  QThreadPool::globalInstance()->setExpiryTimeout(-1);

  if(canHideParent)
    parent->hide();
  GDSplash splash;
  splash.show();
  splash.showUiMsg(LoadDictionaries::tr("Start Loading Dictionaries"));

  dictionaries.clear();

  // Start a thread to load all the dictionaries

  LoadDictionaries loadDicts( cfg, dictionaries, timer );
  QObject::connect(&loadDicts, SIGNAL(showMessage(const QString &, const QColor &)),
                   &splash, SLOT(showMessage(const QString &, const QColor &)), Qt::QueuedConnection  );

  QEventLoop localLoop;

  QObject::connect( &loadDicts, SIGNAL( finished() ),
                    &localLoop, SLOT( quit() ) );

  loadDicts.start();

  localLoop.exec();

  loadDicts.wait();

  QThreadPool::globalInstance()->setExpiryTimeout(expiryTimeout);

  const string &err = loadDicts.getExceptionText();
  if ( !err.empty() )
  {
    QMessageBox::critical( &splash, QCoreApplication::translate( "LoadDictionaries", "Error loading dictionaries" ),
                           QString::fromUtf8( err.c_str() ) );
    if( dictionaries.empty() )
    {
      if(canHideParent)
        parent->show();
      splash.finish(parent);
      return;
    }
  }

  try{
  ///// We create transliterations synchronously since they are very simple
  const QString hmsg("%1 Dictionaries Handled\n[ %3 ]");
#ifdef MAKE_CHINESE_CONVERSION_SUPPORT
  // Make Chinese conversion
  {
    vector< sptr< Dictionary::Class > > chineseDictionaries =
      Chinese::makeDictionaries( cfg.transliteration.chinese );

    dictionaries.insert( dictionaries.end(), chineseDictionaries.begin(),
                         chineseDictionaries.end() );
    if(!chineseDictionaries.empty())
        splash.showUiMsg(hmsg.arg("Chinese-conversion").arg(chineseDictionaries.size()));
  }
#endif

  // Make Romaji
  {
    vector< sptr< Dictionary::Class > > romajiDictionaries =
      Romaji::makeDictionaries( cfg.transliteration.romaji );

    dictionaries.insert( dictionaries.end(), romajiDictionaries.begin(),
                         romajiDictionaries.end() );
    if(!romajiDictionaries.empty())
        splash.showUiMsg(hmsg.arg("Romaji").arg(romajiDictionaries.size()));
  }

  size_t transliteration_dc = dictionaries.size();
  // Make Russian transliteration
  if ( cfg.transliteration.enableRussianTransliteration )
    dictionaries.push_back( RussianTranslit::makeDictionary() );

  // Make German transliteration
  if ( cfg.transliteration.enableGermanTransliteration )
    dictionaries.push_back( GermanTranslit::makeDictionary() );

  // Make Greek transliteration
  if ( cfg.transliteration.enableGreekTransliteration )
    dictionaries.push_back( GreekTranslit::makeDictionary() );

  // Make Belarusian transliteration
  if ( cfg.transliteration.enableBelarusianTransliteration )
  {
    vector< sptr< Dictionary::Class > > dicts = BelarusianTranslit::makeDictionaries();
    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
  }

  transliteration_dc =  dictionaries.size() - transliteration_dc;
  if(transliteration_dc)
    splash.showUiMsg(hmsg.arg("Transliteration").arg(transliteration_dc));

  ///// We create MediaWiki dicts synchronously, since they use netmgr
  {
    vector< sptr< Dictionary::Class > > dicts =
      MediaWiki::makeDictionaries( loadDicts, cfg.mediawikis, dictNetMgr );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
    if(!dicts.empty())
        splash.showUiMsg(hmsg.arg("MediaWiki").arg(dicts.size()));
  }

  ///// WebSites are very simple, no need to create them asynchronously
  {
    vector< sptr< Dictionary::Class > > dicts =
      WebSite::makeDictionaries( cfg.webSites, dictNetMgr );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
    if(!dicts.empty())
        splash.showUiMsg(hmsg.arg("WebSite").arg(dicts.size()));
  }

  //// Forvo dictionaries
  {
    vector< sptr< Dictionary::Class > > dicts =
      Forvo::makeDictionaries( loadDicts, cfg.forvo, dictNetMgr );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
    if(!dicts.empty())
        splash.showUiMsg(hmsg.arg("Forvo").arg(dicts.size()));
  }

  //// Programs
  {
    vector< sptr< Dictionary::Class > > dicts =
      Programs::makeDictionaries( cfg.programs );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
    if(!dicts.empty())
        splash.showUiMsg(hmsg.arg("Programs").arg(dicts.size()));
  }

  //// Text to Speech
  {
    vector< sptr< Dictionary::Class > > dicts =
      VoiceEngines::makeDictionaries( cfg.voiceEngines );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
    if(!dicts.empty())
        splash.showUiMsg(hmsg.arg("Text-to-Speech").arg(dicts.size()));
  }

  {
    vector< sptr< Dictionary::Class > > dicts =
      DictServer::makeDictionaries( cfg.dictServers );

    dictionaries.insert( dictionaries.end(), dicts.begin(), dicts.end() );
    if(!dicts.empty())
        splash.showUiMsg(hmsg.arg("DictServer").arg(dicts.size()));
  }

  GD_DPRINTF( "Load done\n" );

  // Remove any stale index files
  {
  set< string > ids;
  std::pair< std::set< string >::iterator, bool > ret;

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
  QTextCodec::setCodecForCStrings( QTextCodec::codecForName( "UTF8" ) );
#endif

  for( size_t x = dictionaries.size(); x--; )
  {
    ret = ids.insert( dictionaries[ x ]->getId() );
    if( !ret.second )
    {
      gdWarning( "Duplicate dictionary ID found: ID=%s, name=\"%s\", path=\"%s\"",
                 dictionaries[ x ]->getId().c_str(),
                 dictionaries[ x ]->getName().c_str(),
                 dictionaries[ x ]->getDictionaryFilenames().empty() ?
                   "" : dictionaries[ x ]->getDictionaryFilenames()[ 0 ].c_str()
                );
//      splash.showUiMsg(tr("Duplicate Dictionary's ID=%1 found.\nname=\"%2\"\npath=\"%3\"").
//                       arg(QString::fromUtf8(dictionaries[ x ]->getId().c_str())).
//                       arg(QString::fromUtf8(dictionaries[ x ]->getName().c_str())).
//                       arg(QString::fromUtf8(dictionaries[ x ]->getDictionaryFilenames().empty() ?
//                                                 "" : dictionaries[ x ]->getDictionaryFilenames()[ 0 ].c_str())) );
    }
  }

  QDir indexDir( Config::getIndexDir() );

  QStringList allIdxFiles = indexDir.entryList( QDir::Files );

  for( QStringList::const_iterator i = allIdxFiles.constBegin();
       i != allIdxFiles.constEnd(); ++i )
  {
    if ( ids.find( FsEncoding::encode( *i ) ) == ids.end()
         && i->size() == 32 )
      indexDir.remove( *i );
    else
    if ( i->endsWith( "_FTS" )
         && i->size() == 36
         && ids.find( FsEncoding::encode( i->left( 32 ) ) ) == ids.end() )
      indexDir.remove( *i );
  }
  }
  // Run deferred inits

  if ( doDeferredInit_ )
  {
    splash.showUiMsg(LoadDictionaries::tr("Init Dictionaries ..."));
    doDeferredInit( dictionaries );
  }
  }
  catch( std::exception & e )
  {
  }

  if(canHideParent)
    parent->show();
  splash.showUiMsg(LoadDictionaries::tr("Loading Done.\n[ %1 ] Dictionaries Handled.\nTime elapsed: %2 s").
                   arg(dictionaries.size()).
                   arg(timer.elapsed() / 1000));
  splash.finish(parent);
}

void LoadDictionaries::doDeferredInit( std::vector< sptr< Dictionary::Class > > & dictionaries )
{
  for( unsigned x = 0; x < dictionaries.size(); ++x )
    dictionaries[ x ]->deferredInit();
}
