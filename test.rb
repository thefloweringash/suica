require 'felica'
require 'suica'

loop do
  Felica.open_device do |nfc_dev|
    suica = Suica::Suica.new(nfc_dev.select_felica)
    suica.read_transactions.each { |tx| puts tx.inspect }
    sleep 1
  end
end
