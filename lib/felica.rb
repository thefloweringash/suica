module Felica
  class Felica
    def initialize
      @device = ::Felica.open_device
      @device.init!
    end
    def poll
      @device.select_felica
    end
  end

  def self.init!
    @@context = NFC.make_context
  end
  def self.open_device
    @@context.open_device
  end
end

require 'felica/errors'
require 'felica/felica'

Felica::init!
