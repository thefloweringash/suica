Gem::Specification.new do |s|
  s.name     = 'suica'
  s.version  = '0.0.0'
  s.date     = '2016-02-23'
  s.summary  = "Suica transit card inspection utility"
  s.authors  = ['Andrew Childs']
  s.email    = 'lorne@cons.org.nz'
  s.homepage = 'https://github.com/thefloweringash/suica'
  s.license  = 'BSD-2-Clause'

  s.files       = ['lib/felica.rb',
                   'lib/suica.rb',
                   'lib/felica/errors.rb',
                   'ext/felica/felica.cc']
  s.executables = ['suica-import']
  s.extensions  = ['ext/felica/extconf.rb']
end
