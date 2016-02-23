require 'mkmf'

dir_config('nfc',
           ['/usr/local/include', '/usr/include'],
           ['/usr/local/lib',     '/usr/lib'])

abort "Missing libnfc library" unless have_library('nfc')
abort "Missing libnfc headers" unless have_header('nfc/nfc.h')

$CXXFLAGS << ' -std=c++11'

create_makefile('felica/felica')
