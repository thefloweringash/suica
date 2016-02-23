require 'felica'
require 'suica'



f = Felica::Felica.new
while true
  suica = Suica::Suica.new(f.poll())
  suica.read_transactions
  sleep 1
end
