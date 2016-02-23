require 'felica'
require 'suica'

f = Felica::Felica.new
while true
  suica = Suica::Suica.new(f.poll())
  puts suica.read_transactions.map(&:inspect)
  sleep 1
end
